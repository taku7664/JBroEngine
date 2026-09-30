#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Canvas/ScreenSpace.h>

#include <JBro/Runtime/GameObjectHandleReflection.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Reflection/ReflectedYaml.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/ScriptRegistry.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Table.h>

#include <cstdio>
#include <cstring>

namespace JBro
{
    namespace
    {
        constexpr std::uint32_t CanvasFileVersion = 1;

        bool Fail(CanvasFileError& error, const char* message)
        {
            error.message = message;
            return false;
        }

        // 값의 걸음이 채운 오류를 파일 오류로 옮긴다. 오브젝트·타입 이름은 이쪽이 이미 적어 두었다.
        bool FailFrom(CanvasFileError& error, const ReflectedYamlError& reflected)
        {
            error.message = reflected.message;
            error.fieldName = reflected.fieldName;
            return false;
        }

        // 컴포넌트나 스크립트 하나의 키들을 지금 자리에 적는다. 파일은 나열의 항목 안에, 핫 리로드는 맨 위에 적는다(D-268).
        // `instance` 는 그 타입의 객체다 - 프로퍼티 표의 자리는 그 타입 기준이다.
        bool WriteFields(
            YamlWriter& writer,
            const void* instance,
            bool enabled,
            ComponentTypeId typeId,
            CanvasFileError& error)
        {
            const char* typeName = NameTable::Get().Resolve(typeId);
            error.typeName = typeName;

            const PropertyTable* table = PropertyRegistry::Lookup(typeId);
            if (table == nullptr)
            {
                // 등록되지 않은 타입이다. 조용히 빠뜨리면 씬이 하나를 잃은 채로 저장되고 아무도 모른다.
                return Fail(error, "this type never registered its properties");
            }

            // 타입을 먼저 적는다. 읽는 쪽이 무엇을 읽는지 알고 시작한다.
            writer.WriteString("Type", typeName);
            // 오브젝트 활성까지 합친 값을 적으면 꺼진 오브젝트를 저장했다 열 때 **영구히** 꺼진다. 제 켜짐만 적는다.
            writer.WriteBool("IsEnabled", enabled);
            for (std::uint32_t i = 0; i < table->count; ++i)
            {
                const PropertyInfo& property = table->properties[i];
                if (false == property.serialize || property.type == nullptr)
                {
                    continue;
                }
                ReflectedYamlError reflected;
                if (false == WriteReflectedValue(writer, NameTable::Get().Resolve(property.name),
                    *property.type, property.ConstAddress(instance), reflected))
                {
                    return FailFrom(error, reflected);
                }
            }
            error.typeName.clear();
            return true;
        }

        bool WriteComponent(YamlWriter& writer, const ComponentSlot& slot, CanvasFileError& error)
        {
            const ComponentBase* component = slot.reference.TryGet();
            if (component == nullptr)
            {
                return Fail(error, "an object holds a component that is already gone");
            }
            writer.BeginMap(nullptr);
            if (false == WriteFields(writer, component, component->IsEnabled(), slot.typeId, error))
            {
                return false;
            }
            writer.EndMap();
            return true;
        }

        bool WriteScript(YamlWriter& writer, const GameScriptBase* script, ComponentTypeId typeId, CanvasFileError& error)
        {
            if (script == nullptr)
            {
                return Fail(error, "an object holds a script that is already gone");
            }
            writer.BeginMap(nullptr);
            if (false == WriteFields(writer, script, script->IsEnabled(), typeId, error))
            {
                return false;
            }
            writer.EndMap();
            return true;
        }

        // 모르는 스크립트를 읽은 그대로 되쓴다(D-264).
        bool WriteUnresolvedScript(
            YamlWriter& writer,
            const UnresolvedScript& kept,
            CanvasWriteMode mode,
            CanvasFileError& error)
        {
            error.typeName = kept.typeName;
            if (mode == CanvasWriteMode::Package)
            {
                // 게임은 이 스크립트 없이 돈다. 조용히 묶으면 빌드는 되고 게임에서 스크립트 하나가 사라진다.
                return Fail(error, "a script this engine does not know cannot go into a game");
            }
            YamlDocument document;
            YamlError parseError;
            if (false == document.Parse(kept.text.c_str(), kept.text.size(), parseError)
                || document.GetKind(document.GetRoot()) != YamlKind::Map)
            {
                return Fail(error, "a script this engine does not know could not be written back");
            }
            WriteYamlNode(writer, document, document.GetRoot(), nullptr);
            error.typeName.clear();
            return true;
        }

        // 부모가 자식보다 먼저 오게 늘어놓는다. 그래야 ParentIndex 가 언제나 자기 앞을
        // 가리키고, 읽는 쪽이 한 번만 훑어도 계층을 세울 수 있다.
        void CollectInOrder(Object::GameObject* object, Array<Object::GameObject*>& ordered)
        {
            ordered.Add(object);
            const Array<SafePtr<Object::GameObject>>& children = object->GetChildren();
            for (std::size_t i = 0; i < children.Size(); ++i)
            {
                Object::GameObject* child = children[i].TryGet();
                if (child != nullptr)
                {
                    CollectInOrder(child, ordered);
                }
            }
        }

        // 한 오브젝트의 스크립트를 파일에 적는 차례로 늘어놓은 한 칸이다. 붙은 것이거나(`script`) 모르는 것(`kept`)이다.
        struct ScriptEntry
        {
            GameScriptBase* script = nullptr;
            ComponentTypeId typeId = InvalidComponentTypeId;
            const UnresolvedScript* kept = nullptr;
        };

        // 모르는 것은 그 앞에 있던 붙은 것의 개수 자리에 끼운다. 그 뒤로 스크립트를 떼면 자리가 앞당겨진다.
        void OrderScripts(const Object::GameObject& object, const Array<UnresolvedScript>* kept, Array<ScriptEntry>& entries)
        {
            entries.Clear();
            const Array<ScriptSlot>& scripts = object.GetScripts();
            const std::size_t keptCount = kept != nullptr ? kept->Size() : 0;
            std::size_t nextKept = 0;
            for (std::size_t s = 0; s <= scripts.Size(); ++s)
            {
                while (nextKept < keptCount && ((*kept)[nextKept].position <= s || s == scripts.Size()))
                {
                    ScriptEntry entry;
                    entry.kept = &(*kept)[nextKept];
                    entries.Add(entry);
                    ++nextKept;
                }
                if (s < scripts.Size())
                {
                    ScriptEntry entry;
                    entry.script = scripts[s].reference.TryGet();
                    entry.typeId = scripts[s].typeId;
                    entries.Add(entry);
                }
            }
        }

        // 파일의 한 항목을 붙인 것에 읽어 넣는다. `Type`·`IsEnabled` 는 형식의 키라 필드가 아니다.
        bool ReadFields(const YamlDocument& document, std::uint32_t saved, ComponentTypeId typeId, void* instance,
            CanvasFileError& error)
        {
            const PropertyTable* table = PropertyRegistry::Lookup(typeId);
            if (table == nullptr)
            {
                return Fail(error, "this type never registered its properties");
            }
            static const char* const formatKeys[] = { "Type", "IsEnabled" };
            ReflectedYamlError reflected;
            if (false == ReadReflectedFields(document, saved, *table, instance,
                formatKeys, sizeof(formatKeys) / sizeof(formatKeys[0]), reflected))
            {
                return FailFrom(error, reflected);
            }
            return true;
        }
    }

    bool WriteCanvasText(Canvas& canvas, String& text, CanvasFileError& error, CanvasWriteMode mode)
    {
        error = CanvasFileError{};

        // 뿌리부터 자식 순서로 늘어놓는다. 풀 순서는 부모·자식 관계를 모른다.
        // **뿌리의 차례는 캔버스가 들고 있는 보이는 순서다**(D-128) - 풀 순서로 적으면
        // 계층에서 끌어 옮긴 순서가 저장에서 사라진다.
        Array<Object::GameObject*> roots;
        canvas.GetRootObjects(roots);

        Array<Object::GameObject*> ordered;
        for (std::size_t i = 0; i < roots.Size(); ++i)
        {
            CollectInOrder(roots[i], ordered);
        }
        if (ordered.Size() != canvas.GetObjectCount())
        {
            // 부모를 타고 뿌리에 닿지 못한 오브젝트가 있다는 뜻이다. 그대로 적으면
            // 씬이 조용히 일부를 잃는다.
            return Fail(error, "some objects could not be reached from a root");
        }

        Table<const Object::GameObject*, std::size_t> indexOf;
        for (std::size_t i = 0; i < ordered.Size(); ++i)
        {
            indexOf.TryAdd(ordered[i], i);
        }

        // **오브젝트 참조 필드는 파일 안 번호로 적는다**(D-233). 오브젝트 번호는 실행마다 달라 파일에 남길 수 없다.
        Table<InstanceId, std::int64_t> fileIndexOf;
        for (std::size_t i = 0; i < ordered.Size(); ++i)
        {
            fileIndexOf.TryAdd(ordered[i]->GetInstanceId(), static_cast<std::int64_t>(i));
        }
        Internal::ObjectRefRemap remap;
        remap.user = &fileIndexOf;
        remap.toIndex = [](void* user, InstanceId objectId) -> std::int64_t {
            const std::int64_t* found = static_cast<Table<InstanceId, std::int64_t>*>(user)->Find(objectId);
            return found != nullptr ? *found : -1;
        };
        ObjectRefRemapScope remapScope(remap);

        YamlWriter writer;
        writer.WriteInt("Version", static_cast<std::int64_t>(CanvasFileVersion));

        // **배경색은 캔버스의 것이다**(D-186). 네 채널을 한 줄씩 적는다 - 한 줄에 몰아
        // 적으면 사람이 고칠 때 어느 숫자가 무엇인지 세어야 한다.
        {
            const Color& background = canvas.GetBackgroundColor();
            writer.BeginMap("BackgroundColor");
            writer.WriteFloat("R", background.R);
            writer.WriteFloat("G", background.G);
            writer.WriteFloat("B", background.B);
            writer.WriteFloat("A", background.A);
            writer.EndMap();
        }

        writer.BeginSequence("Layers");
        for (std::size_t i = 0; i < canvas.GetLayerCount(); ++i)
        {
            const Layer* layer = canvas.GetLayerAt(i);
            if (layer == nullptr)
            {
                return Fail(error, "a layer went missing while the canvas was being written");
            }
            writer.BeginMap(nullptr);
            writer.WriteInt("Id", static_cast<std::int64_t>(layer->GetId()));
            writer.WriteString("Name", layer->GetName());
            writer.WriteBool("Visible", layer->IsVisible());
            // 기본값(월드·FixedHeight)이면 적지 않는다 - 화면 레이어가 없는 옛 캔버스는 저장해도 그대로다(D-237).
            if (layer->GetSpace() != LayerSpace::World)
            {
                writer.WriteString("Space", LayerSpaceName(layer->GetSpace()));
            }
            if (layer->GetScaleMode() != ScreenScaleMode::FixedHeight)
            {
                writer.WriteString("ScaleMode", ScreenScaleModeName(layer->GetScaleMode()));
            }
            writer.EndMap();
        }
        writer.EndSequence();

        Array<ScriptEntry> scriptEntries;
        writer.BeginSequence("Objects");
        for (std::size_t i = 0; i < ordered.Size(); ++i)
        {
            Object::GameObject* object = ordered[i];
            // 오브젝트의 이름은 태그로 산다 — `Canvas::CreateObject(name)` 이 거기에 넣는다.
            error.objectName = object->GetTag();

            writer.BeginMap(nullptr);
            writer.WriteString("Name", object->GetTag());
            writer.WriteBool("Active", object->IsActiveSelf());
            // 플래그는 있을 때만 적는다 - 대부분의 오브젝트는 0 이고, 없으면 0 으로 읽는다.
            const std::uint32_t flags = mode == CanvasWriteMode::Package
                ? (object->GetFlags() & ~EditorOnlyObjectFlags)
                : object->GetFlags();
            if (flags != 0)
            {
                writer.WriteInt("Flags", static_cast<std::int64_t>(flags));
            }

            std::int64_t parentIndex = -1;
            if (object->GetParent() != nullptr)
            {
                const std::size_t* found = indexOf.Find(object->GetParent());
                if (found == nullptr)
                {
                    return Fail(error, "an object hangs from something that is not in this canvas");
                }
                parentIndex = static_cast<std::int64_t>(*found);
            }
            writer.WriteInt("ParentIndex", parentIndex);
            writer.WriteInt("LayerId", static_cast<std::int64_t>(object->GetLayerId()));

            writer.BeginSequence("Components");
            for (const ComponentSlot& slot : object->GetComponents())
            {
                if (false == WriteComponent(writer, slot, error))
                {
                    return false;
                }
            }
            writer.EndSequence();

            // **스크립트는 따로 적는다**(D-271). 없으면 키를 적지 않는다 - 스크립트가 없는 오브젝트가 대부분이다.
            OrderScripts(*object, canvas.FindUnresolvedScripts(object), scriptEntries);
            if (false == scriptEntries.IsEmpty())
            {
                writer.BeginSequence("Scripts");
                for (const ScriptEntry& entry : scriptEntries)
                {
                    if (entry.kept != nullptr)
                    {
                        if (false == WriteUnresolvedScript(writer, *entry.kept, mode, error))
                        {
                            return false;
                        }
                        continue;
                    }
                    if (false == WriteScript(writer, entry.script, entry.typeId, error))
                    {
                        return false;
                    }
                }
                writer.EndSequence();
            }
            writer.EndMap();
        }
        writer.EndSequence();

        error.objectName.clear();
        text = writer.GetText();
        return true;
    }

    // -----------------------------------------------------------------------
    // 읽기
    // -----------------------------------------------------------------------

    bool ReadCanvasText(Canvas& canvas, const char* text, std::size_t length, CanvasFileError& error)
    {
        error = CanvasFileError{};
        if (canvas.GetObjectCount() != 0)
        {
            return Fail(error, "a canvas must be empty before a file is read into it");
        }

        YamlDocument document;
        YamlError parseError;
        if (false == document.Parse(text, length, parseError))
        {
            error.message = parseError.message;
            return false;
        }

        const std::uint32_t root = document.GetRoot();
        std::int64_t version = 0;
        if (false == document.FindInt(root, "Version", version))
        {
            return Fail(error, "this file does not say what version it is");
        }
        if (version != static_cast<std::int64_t>(CanvasFileVersion))
        {
            return Fail(error, "this file was written by a different version of the format");
        }

        // 배경색은 **없어도 된다**(D-186). 이 키가 생기기 전의 파일은 캔버스의 기본값으로
        // 열린다 - 그것이 그때 화면에 나오던 색이다.
        {
            const std::uint32_t background = document.Find(root, "BackgroundColor");
            if (background != YamlDocument::InvalidNode)
            {
                Color color = canvas.GetBackgroundColor();
                document.FindFloat(background, "R", color.R);
                document.FindFloat(background, "G", color.G);
                document.FindFloat(background, "B", color.B);
                document.FindFloat(background, "A", color.A);
                canvas.SetBackgroundColor(color);
            }
        }

        // 레이어부터 만든다. 파일의 Id 는 그대로 쓸 수 없으므로(캔버스가 스스로 매긴다)
        // 파일의 것과 새로 받은 것을 짝지어 둔다.
        Table<std::uint64_t, LayerId> layerOf;
        const std::uint32_t layers = document.Find(root, "Layers");
        bool firstLayer = true;
        for (std::size_t i = 0; i < document.GetCount(layers); ++i)
        {
            const std::uint32_t entry = document.GetElement(layers, i);
            String name;
            std::int64_t fileId = 0;
            if (false == document.FindScalar(entry, "Name", name)
                || false == document.FindInt(entry, "Id", fileId))
            {
                return Fail(error, "a layer in this file has no name or no id");
            }
            bool visible = true;
            document.FindBool(entry, "Visible", visible);
            LayerSpace space = LayerSpace::World;
            ScreenScaleMode scaleMode = ScreenScaleMode::FixedHeight;
            String spaceName;
            if (document.FindScalar(entry, "Space", spaceName) && false == ParseLayerSpace(spaceName.c_str(), space))
            {
                return Fail(error, "a layer names a space this engine does not know");
            }
            String scaleName;
            if (document.FindScalar(entry, "ScaleMode", scaleName) && false == ParseScreenScaleMode(scaleName.c_str(), scaleMode))
            {
                return Fail(error, "a layer names a screen scale mode this engine does not know");
            }

            // 캔버스는 기본 레이어를 하나 들고 시작한다. 첫 레이어는 그것을 쓴다 —
            // 그러지 않으면 파일을 읽을 때마다 쓰지 않는 레이어가 하나씩 남는다.
            Layer* layer = firstLayer ? canvas.FindLayer(canvas.GetDefaultLayer()) : nullptr;
            if (layer == nullptr)
            {
                layer = &canvas.CreateLayer(name.c_str());
            }
            else
            {
                layer->SetName(name.c_str());
            }
            firstLayer = false;
            layer->SetVisible(visible);
            layer->SetSpace(space);
            layer->SetScaleMode(scaleMode);
            layerOf.TryAdd(static_cast<std::uint64_t>(fileId), layer->GetId());
        }

        // **오브젝트를 모두 만든 뒤 컴포넌트를 읽는다**(D-233). 오브젝트 참조 필드는 뒤에 오는 오브젝트도 가리킬 수 있다.
        const std::uint32_t objects = document.Find(root, "Objects");
        Array<Object::GameObject*> created;
        for (std::size_t i = 0; i < document.GetCount(objects); ++i)
        {
            const std::uint32_t entry = document.GetElement(objects, i);
            String name;
            document.FindScalar(entry, "Name", name);
            error.objectName = name;

            Object::GameObject* object = canvas.CreateObject(name.c_str());
            if (object == nullptr)
            {
                return Fail(error, "an object in this file could not be created");
            }
            created.Add(object);
            std::int64_t flags = 0;
            if (document.FindInt(entry, "Flags", flags))
            {
                if (flags < 0 || flags > static_cast<std::int64_t>(UINT32_MAX))
                {
                    return Fail(error, "an object in this file has flags that do not fit");
                }
                object->SetFlags(static_cast<std::uint32_t>(flags));
            }

            std::int64_t parentIndex = -1;
            if (false == document.FindInt(entry, "ParentIndex", parentIndex))
            {
                return Fail(error, "an object in this file does not say where it hangs");
            }
            if (parentIndex >= 0)
            {
                // 부모는 언제나 자기 앞에 있다. 저장이 그 순서를 지키므로 한 번만 훑으면 된다.
                if (static_cast<std::size_t>(parentIndex) >= created.Size() - 1)
                {
                    return Fail(error, "an object hangs from something that comes after it");
                }
                object->SetParent(created[static_cast<std::size_t>(parentIndex)]);
            }

            std::int64_t fileLayer = 0;
            if (document.FindInt(entry, "LayerId", fileLayer))
            {
                const LayerId* mapped = layerOf.Find(static_cast<std::uint64_t>(fileLayer));
                if (mapped == nullptr)
                {
                    return Fail(error, "an object sits on a layer this file never described");
                }
                canvas.SetObjectLayer(object, *mapped);
            }
        }

        // 모르는 스크립트가 들고 있는 파일 안 번호를 뒤에 풀 수 있게 이 파일의 차례를 캔버스에 둔다(D-268).
        {
            Array<InstanceId> order;
            order.Reserve(created.Size());
            for (Object::GameObject* object : created)
            {
                order.Add(object->GetInstanceId());
            }
            canvas.SetFileObjectOrder(std::move(order));
        }

        Internal::ObjectRefRemap remap;
        remap.user = &created;
        remap.toObjectId = [](void* user, std::int64_t index) -> InstanceId {
            const Array<Object::GameObject*>& objects = *static_cast<Array<Object::GameObject*>*>(user);
            return index >= 0 && static_cast<std::size_t>(index) < objects.Size()
                ? objects[static_cast<std::size_t>(index)]->GetInstanceId()
                : InvalidInstanceId;
        };
        ObjectRefRemapScope remapScope(remap);
        for (std::size_t i = 0; i < created.Size(); ++i)
        {
            const std::uint32_t entry = document.GetElement(objects, i);
            Object::GameObject* object = created[i];
            error.objectName = object->GetTag();

            const std::uint32_t components = document.Find(entry, "Components");
            for (std::size_t c = 0; c < document.GetCount(components); ++c)
            {
                const std::uint32_t saved = document.GetElement(components, c);
                String typeName;
                if (false == document.FindScalar(saved, "Type", typeName))
                {
                    return Fail(error, "a component in this file does not say what it is");
                }
                error.typeName = typeName;

                const ComponentTypeInfo* info = ComponentRegistry::Get().Find(MakeNameId(typeName.c_str()));
                if (info == nullptr)
                {
                    // **스크립트는 `Scripts` 에 있어야 한다**(D-271). 둘이 한 목록이던 파일은 옮겨 읽지 않는다(사용자 결정 2026-09-29).
                    return Fail(error, ScriptRegistry::Get().Find(MakeNameId(typeName.c_str())) != nullptr
                        ? "a script is listed among the components - this file is from before scripts were separated from components"
                        : "a component in this file is not one this engine knows");
                }
                ComponentBase* component = info->Attach(canvas, object);
                if (component == nullptr)
                {
                    return Fail(error, "a component in this file could not be attached");
                }
                if (false == ReadFields(document, saved, info->typeId, component, error))
                {
                    return false;
                }
                bool enabled = true;
                if (document.FindBool(saved, "IsEnabled", enabled))
                {
                    component->SetEnabled(enabled);
                }
                error.typeName.clear();
            }

            const std::uint32_t scripts = document.Find(entry, "Scripts");
            std::uint32_t resolvedCount = 0;
            for (std::size_t s = 0; s < document.GetCount(scripts); ++s)
            {
                const std::uint32_t saved = document.GetElement(scripts, s);
                String typeName;
                if (false == document.FindScalar(saved, "Type", typeName))
                {
                    return Fail(error, "a script in this file does not say what it is");
                }
                error.typeName = typeName;

                const NameId name = MakeNameId(typeName.c_str());
                const ScriptTypeInfo* type = ScriptRegistry::Get().Find(name);
                if (type == nullptr)
                {
                    // **모르는 이름은 멈추지도 버리지도 않는다**(D-264). DLL 을 아직 빌드하지 않은 프로젝트도 캔버스를 연다.
                    YamlWriter fragment;
                    for (std::size_t key = 0; key < document.GetCount(saved); ++key)
                    {
                        WriteYamlNode(fragment, document, document.GetValue(saved, key), document.GetKey(saved, key));
                    }
                    UnresolvedScript kept;
                    kept.typeName = typeName;
                    kept.text = fragment.GetText();
                    kept.position = resolvedCount;
                    if (false == canvas.AddUnresolvedScript(object, std::move(kept)))
                    {
                        return Fail(error, "a script this engine does not know could not be kept");
                    }
                    error.typeName.clear();
                    continue;
                }
                GameScriptBase* script = canvas.AttachScript(object, name);
                if (script == nullptr)
                {
                    return Fail(error, "a script in this file could not be attached");
                }
                ++resolvedCount;
                if (false == ReadFields(document, saved, type->typeId, script, error))
                {
                    return false;
                }
                bool enabled = true;
                if (document.FindBool(saved, "IsEnabled", enabled))
                {
                    script->SetEnabled(enabled);
                }
                error.typeName.clear();
            }

            // 활성은 마지막이다. 부모가 정해진 뒤라야 상속 활성값이 맞는다.
            bool active = true;
            document.FindBool(entry, "Active", active);
            object->SetActive(active);
        }

        error.objectName.clear();
        return true;
    }


    namespace
    {
        void CollectAllObjects(Canvas& canvas, Array<Object::GameObject*>& ordered)
        {
            Array<Object::GameObject*> roots;
            canvas.GetRootObjects(roots);
            for (Object::GameObject* root : roots)
            {
                CollectInOrder(root, ordered);
            }
        }

        const PropertyInfo* FindSerializedProperty(const PropertyTable& table, const char* key)
        {
            const NameId name = MakeNameId(key);
            for (std::uint32_t index = 0; index < table.count; ++index)
            {
                const PropertyInfo& property = table.properties[index];
                if (property.name == name && property.serialize && property.type != nullptr)
                {
                    return &property;
                }
            }
            return nullptr;
        }
    }

    bool KeepScriptsAsText(Canvas& canvas, std::size_t& kept, CanvasFileError& error)
    {
        error = CanvasFileError{};
        kept = 0;
        Array<Object::GameObject*> ordered;
        CollectAllObjects(canvas, ordered);

        // 오브젝트 참조를 이번 실행의 번호로 뜬다. 파일 안 번호는 이 글자가 파일이 아니라서 뜻이 없다.
        const Internal::ObjectRefRemap runtimeIds;
        ObjectRefRemapScope remapScope(runtimeIds);

        // **먼저 모두 뜬다.** 하나라도 실패하면 아무것도 떼지 않는다.
        struct Replacement
        {
            Object::GameObject* object = nullptr;
            Array<UnresolvedScript> scripts;
        };
        Array<Replacement> replacements;
        Array<ScriptEntry> entries;
        for (Object::GameObject* object : ordered)
        {
            error.objectName = object->GetTag();
            OrderScripts(*object, canvas.FindUnresolvedScripts(object), entries);
            bool changes = false;
            for (const ScriptEntry& entry : entries)
            {
                changes = changes || (entry.script != nullptr && canvas.IsModuleScript(entry.script));
            }
            if (false == changes)
            {
                continue;
            }
            Replacement replacement;
            replacement.object = object;
            std::uint32_t resolvedBefore = 0;
            for (const ScriptEntry& entry : entries)
            {
                if (entry.kept != nullptr)
                {
                    UnresolvedScript copy = *entry.kept;
                    copy.position = resolvedBefore;
                    replacement.scripts.Add(std::move(copy));
                    continue;
                }
                if (entry.script == nullptr || false == canvas.IsModuleScript(entry.script))
                {
                    ++resolvedBefore;
                    continue;
                }
                YamlWriter writer;
                if (false == WriteFields(writer, entry.script, entry.script->IsEnabled(), entry.typeId, error))
                {
                    return false;
                }
                UnresolvedScript script;
                script.typeName = NameTable::Get().Resolve(entry.typeId);
                script.text = writer.GetText();
                script.position = resolvedBefore;
                script.componentId = entry.script->GetInstanceId();
                replacement.scripts.Add(std::move(script));
                ++kept;
            }
            replacements.Add(std::move(replacement));
        }
        error.objectName.clear();

        for (Replacement& replacement : replacements)
        {
            canvas.ReplaceUnresolvedScripts(replacement.object, std::move(replacement.scripts));
        }
        canvas.ReleaseModuleScripts();
        return true;
    }

    std::size_t ResolveKeptScripts(Canvas& canvas, Array<ScriptResolveNote>& notes)
    {
        notes.Clear();
        Array<Object::GameObject*> ordered;
        CollectAllObjects(canvas, ordered);

        // 파일에서 온 것은 파일 안 번호를 들고 있다. 그 파일을 읽을 때의 차례로 푼다. 핫 리로드가 뜬 것은 `@번호` 라 이것을 보지 않는다.
        Internal::ObjectRefRemap remap;
        remap.user = const_cast<Array<InstanceId>*>(&canvas.GetFileObjectOrder());
        remap.toObjectId = [](void* user, std::int64_t index) -> InstanceId {
            const Array<InstanceId>& order = *static_cast<const Array<InstanceId>*>(user);
            return index >= 0 && static_cast<std::size_t>(index) < order.Size()
                ? order[static_cast<std::size_t>(index)]
                : InvalidInstanceId;
        };
        ObjectRefRemapScope remapScope(remap);

        std::size_t resolved = 0;
        Array<ScriptEntry> entries;
        for (Object::GameObject* object : ordered)
        {
            const Array<UnresolvedScript>* existing = canvas.FindUnresolvedScripts(object);
            if (existing == nullptr || existing->IsEmpty())
            {
                continue;
            }
            // 붙이는 동안 원래 목록이 바뀌므로 사본으로 걷는다.
            const Array<UnresolvedScript> keptCopy = *existing;
            OrderScripts(*object, &keptCopy, entries);

            Array<UnresolvedScript> remaining;
            std::size_t resolvedBefore = 0;
            bool changed = false;
            for (const ScriptEntry& entry : entries)
            {
                if (entry.kept == nullptr)
                {
                    ++resolvedBefore;
                    continue;
                }
                const UnresolvedScript& kept = *entry.kept;
                const NameId name = MakeNameId(kept.typeName.c_str());
                const ScriptTypeInfo* type = ScriptRegistry::Get().Find(name);
                YamlDocument document;
                YamlError parseError;
                const bool known = type != nullptr
                    && document.Parse(kept.text.c_str(), kept.text.size(), parseError)
                    && document.GetKind(document.GetRoot()) == YamlKind::Map;
                GameScriptBase* script = nullptr;
                if (known)
                {
                    script = canvas.AttachScript(object, name, kept.componentId);
                    if (script == nullptr)
                    {
                        ScriptResolveNote note;
                        note.kind = ScriptResolveNote::Kind::NotAttached;
                        note.objectName = object->GetTag();
                        note.typeName = kept.typeName;
                        notes.Add(std::move(note));
                    }
                }
                const PropertyTable* table = script != nullptr ? PropertyRegistry::Lookup(type->typeId) : nullptr;
                if (script == nullptr || table == nullptr)
                {
                    if (script != nullptr)
                    {
                        canvas.DetachScript(object, script);
                    }
                    UnresolvedScript copy = kept;
                    copy.position = static_cast<std::uint32_t>(resolvedBefore);
                    remaining.Add(std::move(copy));
                    continue;
                }

                const std::uint32_t root = document.GetRoot();
                for (std::size_t key = 0; key < document.GetCount(root); ++key)
                {
                    const char* fieldName = document.GetKey(root, key);
                    if (std::strcmp(fieldName, "Type") == 0 || std::strcmp(fieldName, "IsEnabled") == 0)
                    {
                        continue;
                    }
                    const PropertyInfo* property = FindSerializedProperty(*table, fieldName);
                    ReflectedYamlError reflected;
                    if (property != nullptr
                        && ReadReflectedValue(document, document.GetValue(root, key), *property->type,
                            property->Address(script), reflected))
                    {
                        continue;
                    }
                    ScriptResolveNote note;
                    note.kind = property == nullptr ? ScriptResolveNote::Kind::FieldDropped
                                                    : ScriptResolveNote::Kind::FieldUnreadable;
                    note.objectName = object->GetTag();
                    note.typeName = kept.typeName;
                    note.fieldName = fieldName;
                    notes.Add(std::move(note));
                }
                bool enabled = true;
                if (document.FindBool(root, "IsEnabled", enabled))
                {
                    script->SetEnabled(enabled);
                }
                // 붙이면 맨 뒤다. 들고 있던 자리로 옮긴다.
                object->SetScriptIndex(script, resolvedBefore);
                ++resolvedBefore;
                ++resolved;
                changed = true;
            }
            if (changed)
            {
                canvas.ReplaceUnresolvedScripts(object, std::move(remaining));
            }
        }
        return resolved;
    }
}
