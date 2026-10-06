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
#include <JBro/Types/Array.h>
#include <JBro/Types/Table.h>
#include <JBro/Types/Uuid.h>

#include <cstdio>
#include <cstring>

namespace JBro
{
    namespace
    {
        constexpr std::uint32_t CanvasFileVersion = 1;
        // 레이어 에셋의 판이다. 캔버스 파일과 따로 센다(기존 엔진도 그랬다).
        constexpr std::uint32_t LayerFileVersion = 1;

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

        bool WriteComponent(
            YamlWriter& writer,
            const ComponentSlot& slot,
            CanvasFileError& error)
        {
            const ComponentBase* component = slot.reference.TryGet();
            if (component == nullptr)
            {
                return Fail(error, "an object holds a component that is already gone");
            }
            const char* typeName = NameTable::Get().Resolve(slot.typeId);
            error.typeName = typeName;

            const PropertyTable* table = PropertyRegistry::Lookup(slot.typeId);
            if (table == nullptr)
            {
                // 등록되지 않은 타입이다. 조용히 빠뜨리면 씬이 한 컴포넌트를 잃은 채로
                // 저장되고 아무도 모른다.
                return Fail(error, "this component type never registered its properties");
            }

            writer.BeginMap(nullptr);
            // 타입을 먼저 적는다. 읽는 쪽이 무엇을 읽는지 알고 시작한다.
            writer.WriteString("Type", typeName);
            // IsActiveComponent 는 오브젝트 활성까지 합친 값이다. 그것을 적으면
            // 꺼진 오브젝트를 저장했다 열 때 컴포넌트가 **영구히** 꺼진다.
            writer.WriteBool("IsEnabled", component->IsEnabled());
            for (std::uint32_t i = 0; i < table->count; ++i)
            {
                const PropertyInfo& property = table->properties[i];
                if (false == property.serialize || property.type == nullptr)
                {
                    continue;
                }
                ReflectedYamlError reflected;
                if (false == WriteReflectedValue(writer, NameTable::Get().Resolve(property.name),
                    *property.type, property.ConstAddress(component), reflected))
                {
                    return FailFrom(error, reflected);
                }
            }
            writer.EndMap();
            error.typeName.clear();
            return true;
        }

        // 부모가 자식보다 먼저 오게 늘어놓는다. 그래야 ParentIndex 가 언제나 자기 앞을
        // 가리키고, 읽는 쪽이 한 번만 훑어도 계층을 세울 수 있다.
        void CollectInOrder(GameObject* object, Array<GameObject*>& ordered)
        {
            ordered.Add(object);
            const Array<SafePtr<GameObject>>& children = object->GetChildren();
            for (std::size_t i = 0; i < children.Size(); ++i)
            {
                GameObject* child = children[i].TryGet();
                if (child != nullptr)
                {
                    CollectInOrder(child, ordered);
                }
            }
        }

        // **레이어 노드는 캔버스 파일과 레이어 에셋이 같은 함수로 쓰고 읽는다**(D-287). 둘이 갈리면 같은 레이어가 저장한 곳에 따라 다르게 읽힌다
        // (기존 엔진 `LayerSerializer.h` 의 경고). `inCanvas` 면 캔버스 안 번호(`Id`)와 원본 에셋(`SourceAsset`)도 적는다 - 레이어 에셋은 제 자신을
        // 가리키지 않는다. 기본값인 값은 적지 않는다(D-237·D-279·D-286).
        void WriteLayerNode(YamlWriter& writer, const Layer& layer, bool inCanvas)
        {
            if (inCanvas)
            {
                writer.WriteInt("Id", static_cast<std::int64_t>(layer.GetId()));
            }
            writer.WriteString("Name", layer.GetName());
            writer.WriteBool("Visible", layer.IsVisible());
            if (layer.GetSpace() != LayerSpace::World)
            {
                writer.WriteString("Space", LayerSpaceName(layer.GetSpace()));
            }
            if (layer.GetScaleMode() != ScreenScaleMode::FixedHeight)
            {
                writer.WriteString("ScaleMode", ScreenScaleModeName(layer.GetScaleMode()));
            }
            if (layer.GetBlend() != LayerBlend::Normal)
            {
                writer.WriteString("Blend", LayerBlendName(layer.GetBlend()));
            }
            if (layer.GetOpacity() < 1.0f)
            {
                writer.WriteFloat("Opacity", layer.GetOpacity());
            }
            if (layer.GetParallax() != 1.0f)
            {
                writer.WriteFloat("Parallax", layer.GetParallax());
            }
            if (inCanvas && false == layer.GetSourceAsset().IsNull())
            {
                char id[Uuid::TextCapacity] = {};
                layer.GetSourceAsset().ToText(id, sizeof(id));
                writer.WriteString("SourceAsset", id);
            }
        }

        struct LayerNodeValues
        {
            String name;
            bool visible = true;
            LayerSpace space = LayerSpace::World;
            ScreenScaleMode scaleMode = ScreenScaleMode::FixedHeight;
            LayerBlend blend = LayerBlend::Normal;
            float opacity = 1.0f;
            float parallax = 1.0f;
            Uuid sourceAsset;
        };

        bool ReadLayerNode(const YamlDocument& document, std::uint32_t entry, LayerNodeValues& values, CanvasFileError& error)
        {
            if (false == document.FindScalar(entry, "Name", values.name))
            {
                return Fail(error, "a layer in this file has no name or no id");
            }
            document.FindBool(entry, "Visible", values.visible);
            String spaceName;
            if (document.FindScalar(entry, "Space", spaceName) && false == ParseLayerSpace(spaceName.c_str(), values.space))
            {
                return Fail(error, "a layer names a space this engine does not know");
            }
            String scaleName;
            if (document.FindScalar(entry, "ScaleMode", scaleName) && false == ParseScreenScaleMode(scaleName.c_str(), values.scaleMode))
            {
                return Fail(error, "a layer names a screen scale mode this engine does not know");
            }
            String blendName;
            if (document.FindScalar(entry, "Blend", blendName) && false == ParseLayerBlend(blendName.c_str(), values.blend))
            {
                return Fail(error, "a layer names a blend this engine does not know");
            }
            document.FindFloat(entry, "Opacity", values.opacity);
            document.FindFloat(entry, "Parallax", values.parallax);
            String source;
            if (document.FindScalar(entry, "SourceAsset", source)
                && false == Uuid::Parse(source.c_str(), source.size(), values.sourceAsset))
            {
                return Fail(error, "a layer names its source asset with an id that does not read");
            }
            return true;
        }

        void ApplyLayerNode(Layer& layer, const LayerNodeValues& values)
        {
            layer.SetName(values.name.c_str());
            layer.SetVisible(values.visible);
            layer.SetSpace(values.space);
            layer.SetScaleMode(values.scaleMode);
            layer.SetBlend(values.blend);
            layer.SetOpacity(values.opacity);
            layer.SetParallax(values.parallax);
            layer.SetSourceAsset(values.sourceAsset);
        }

        // 오브젝트 목록을 적는다. `ordered` 는 부모가 자식보다 앞이다. `writeLayer` 면 오브젝트마다 레이어 번호를 적는다(캔버스 파일) - 레이어 에셋은
        // 오브젝트가 모두 그 레이어라 적지 않는다.
        bool WriteObjects(YamlWriter& writer, const Array<GameObject*>& ordered, Table<const GameObject*, std::size_t>& indexOf,
            CanvasWriteMode mode, bool writeLayer, CanvasFileError& error)
        {
            writer.BeginSequence("Objects");
            for (std::size_t i = 0; i < ordered.Size(); ++i)
            {
                GameObject* object = ordered[i];
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
                if (writeLayer)
                {
                    writer.WriteInt("LayerId", static_cast<std::int64_t>(object->GetLayerId()));
                }

                writer.BeginSequence("Components");
                const Array<ComponentSlot>& components = object->GetComponents();
                for (std::size_t c = 0; c < components.Size(); ++c)
                {
                    if (false == WriteComponent(writer, components[c], error))
                    {
                        return false;
                    }
                }
                writer.EndSequence();
                writer.EndMap();
            }
            writer.EndSequence();
            error.objectName.clear();
            return true;
        }

        // 오브젝트가 어느 레이어에 서는가. 캔버스 파일은 파일의 레이어 번호를 짝지은 표로, 레이어 에셋은 새로 세운 레이어 하나로 정한다.
        struct ObjectLayerRule
        {
            const Table<std::uint64_t, LayerId>* layerOf = nullptr;
            LayerId fixedLayer = InvalidLayerId;
        };

        // 오브젝트를 만든다. **모두 만든 뒤 컴포넌트를 읽는다**(D-233) - 오브젝트 참조 필드는 뒤에 오는 오브젝트도 가리킬 수 있다. 실패해도 만든 것은
        // `created` 에 남는다 - 거두는 것은 부르는 쪽이다.
        bool ReadObjects(Canvas& canvas, const YamlDocument& document, std::uint32_t objects, const ObjectLayerRule& rule,
            Array<GameObject*>& created, CanvasFileError& error)
        {
            for (std::size_t i = 0; i < document.GetCount(objects); ++i)
            {
                const std::uint32_t entry = document.GetElement(objects, i);
                String name;
                document.FindScalar(entry, "Name", name);
                error.objectName = name;

                GameObject* object = canvas.CreateObject(name.c_str());
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

                if (rule.fixedLayer != InvalidLayerId)
                {
                    canvas.SetObjectLayer(object, rule.fixedLayer);
                }
                else
                {
                    std::int64_t fileLayer = 0;
                    if (rule.layerOf != nullptr && document.FindInt(entry, "LayerId", fileLayer))
                    {
                        const LayerId* mapped = rule.layerOf->Find(static_cast<std::uint64_t>(fileLayer));
                        if (mapped == nullptr)
                        {
                            return Fail(error, "an object sits on a layer this file never described");
                        }
                        canvas.SetObjectLayer(object, *mapped);
                    }
                }
            }

            Internal::ObjectRefRemap remap;
            remap.user = &created;
            remap.toObjectId = [](void* user, std::int64_t index) -> InstanceId {
                const Array<GameObject*>& objects = *static_cast<Array<GameObject*>*>(user);
                return index >= 0 && static_cast<std::size_t>(index) < objects.Size()
                    ? objects[static_cast<std::size_t>(index)]->GetInstanceId()
                    : InvalidInstanceId;
            };
            ObjectRefRemapScope remapScope(remap);
            for (std::size_t i = 0; i < created.Size(); ++i)
            {
                const std::uint32_t entry = document.GetElement(objects, i);
                GameObject* object = created[i];
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

                    const ComponentTypeInfo* info = ComponentRegistry::Get().Find(typeName.c_str());
                    if (info == nullptr)
                    {
                        return Fail(error, "this engine has no component by that name");
                    }
                    ComponentBase* component = info->Attach(canvas, object);
                    if (component == nullptr)
                    {
                        return Fail(error, "a component in this file could not be attached");
                    }

                    const PropertyTable* table = PropertyRegistry::Lookup(info->typeId);
                    if (table == nullptr)
                    {
                        return Fail(error, "this component type never registered its properties");
                    }

                    static const char* const formatKeys[] = { "Type", "IsEnabled" };
                    ReflectedYamlError reflected;
                    if (false == ReadReflectedFields(document, saved, *table, component,
                        formatKeys, sizeof(formatKeys) / sizeof(formatKeys[0]), reflected))
                    {
                        return FailFrom(error, reflected);
                    }

                    bool enabled = true;
                    if (document.FindBool(saved, "IsEnabled", enabled))
                    {
                        component->SetEnabled(enabled);
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

        // 오브젝트 참조를 파일 안 번호로 적는 표다(D-233). 오브젝트 번호는 실행마다 달라 파일에 남길 수 없다. 목록 밖을 가리키면 -1 이다.
        struct FileIndexMap
        {
            explicit FileIndexMap(const Array<GameObject*>& ordered)
            {
                for (std::size_t i = 0; i < ordered.Size(); ++i)
                {
                    fileIndexOf.TryAdd(ordered[i]->GetInstanceId(), static_cast<std::int64_t>(i));
                }
                remap.user = &fileIndexOf;
                remap.toIndex = [](void* user, InstanceId objectId) -> std::int64_t {
                    const std::int64_t* found = static_cast<Table<InstanceId, std::int64_t>*>(user)->Find(objectId);
                    return found != nullptr ? *found : -1;
                };
            }

            Table<InstanceId, std::int64_t> fileIndexOf;
            Internal::ObjectRefRemap remap;
        };
    }

    bool WriteCanvasText(Canvas& canvas, String& text, CanvasFileError& error, CanvasWriteMode mode)
    {
        error = CanvasFileError{};

        // 뿌리부터 자식 순서로 늘어놓는다. 풀 순서는 부모·자식 관계를 모른다.
        // **뿌리의 차례는 캔버스가 들고 있는 보이는 순서다**(D-128) - 풀 순서로 적으면
        // 계층에서 끌어 옮긴 순서가 저장에서 사라진다.
        Array<GameObject*> roots;
        canvas.GetRootObjects(roots);

        Array<GameObject*> ordered;
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

        Table<const GameObject*, std::size_t> indexOf;
        for (std::size_t i = 0; i < ordered.Size(); ++i)
        {
            indexOf.TryAdd(ordered[i], i);
        }

        FileIndexMap fileIndex(ordered);
        ObjectRefRemapScope remapScope(fileIndex.remap);

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
            WriteLayerNode(writer, *layer, true);
            writer.EndMap();
        }
        writer.EndSequence();

        if (false == WriteObjects(writer, ordered, indexOf, mode, true, error))
        {
            return false;
        }

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
            LayerNodeValues values;
            if (false == ReadLayerNode(document, entry, values, error))
            {
                return false;
            }
            std::int64_t fileId = 0;
            if (false == document.FindInt(entry, "Id", fileId))
            {
                return Fail(error, "a layer in this file has no name or no id");
            }
            // 캔버스는 기본 레이어를 하나 들고 시작한다. 첫 레이어는 그것을 쓴다 —
            // 그러지 않으면 파일을 읽을 때마다 쓰지 않는 레이어가 하나씩 남는다.
            Layer* layer = firstLayer ? canvas.FindLayer(canvas.GetDefaultLayer()) : nullptr;
            if (layer == nullptr)
            {
                layer = &canvas.CreateLayer(values.name.c_str());
            }
            firstLayer = false;
            ApplyLayerNode(*layer, values);
            layerOf.TryAdd(static_cast<std::uint64_t>(fileId), layer->GetId());
        }

        Array<GameObject*> created;
        ObjectLayerRule rule;
        rule.layerOf = &layerOf;
        if (false == ReadObjects(canvas, document, document.Find(root, "Objects"), rule, created, error))
        {
            return false;
        }
        error.objectName.clear();
        return true;
    }

    // -----------------------------------------------------------------------
    // 레이어 에셋(`.jlayer`, D-287)
    // -----------------------------------------------------------------------

    bool WriteLayerText(Canvas& canvas, LayerId layerId, String& text, CanvasFileError& error)
    {
        error = CanvasFileError{};
        const Layer* layer = canvas.FindLayer(layerId);
        if (layer == nullptr)
        {
            return Fail(error, "there is no such layer in this canvas");
        }
        // 이 레이어의 뿌리를 캔버스의 보이는 순서대로, 자식을 그 뒤에. 부분 트리는 늘 한 레이어다(D-135).
        Array<GameObject*> roots;
        canvas.GetRootObjects(roots);
        Array<GameObject*> ordered;
        for (std::size_t i = 0; i < roots.Size(); ++i)
        {
            if (roots[i]->GetLayerId() == layerId)
            {
                CollectInOrder(roots[i], ordered);
            }
        }
        Table<const GameObject*, std::size_t> indexOf;
        for (std::size_t i = 0; i < ordered.Size(); ++i)
        {
            indexOf.TryAdd(ordered[i], i);
        }
        // 레이어 밖을 가리키는 오브젝트 참조는 -1(비어 있음)로 적힌다 - 그 오브젝트는 이 파일에 없다.
        FileIndexMap fileIndex(ordered);
        ObjectRefRemapScope remapScope(fileIndex.remap);

        YamlWriter writer;
        writer.WriteInt("Version", static_cast<std::int64_t>(LayerFileVersion));
        writer.BeginMap("Layer");
        WriteLayerNode(writer, *layer, false);
        writer.EndMap();
        if (false == WriteObjects(writer, ordered, indexOf, CanvasWriteMode::Editor, false, error))
        {
            return false;
        }
        text = writer.GetText();
        return true;
    }

    bool ReadLayerText(Canvas& canvas, const char* text, std::size_t length, LayerId& created, CanvasFileError& error)
    {
        error = CanvasFileError{};
        created = InvalidLayerId;
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
        if (version != static_cast<std::int64_t>(LayerFileVersion))
        {
            return Fail(error, "this file was written by a different version of the format");
        }
        const std::uint32_t node = document.Find(root, "Layer");
        if (node == YamlDocument::InvalidNode)
        {
            return Fail(error, "this file holds no layer");
        }
        LayerNodeValues values;
        if (false == ReadLayerNode(document, node, values, error))
        {
            return false;
        }
        // 맨 위(맨 앞)에 새 레이어로 선다(기존 엔진과 같다).
        Layer& layer = canvas.CreateLayer(values.name.c_str());
        ApplyLayerNode(layer, values);
        Array<GameObject*> objects;
        ObjectLayerRule rule;
        rule.fixedLayer = layer.GetId();
        if (false == ReadObjects(canvas, document, document.Find(root, "Objects"), rule, objects, error))
        {
            // **반쯤 들어온 것을 남기지 않는다.** 만든 오브젝트와 레이어를 거둔다 - 캔버스는 읽기 전 그대로다.
            for (std::size_t i = objects.Size(); i > 0; --i)
            {
                if (objects[i - 1]->GetParent() == nullptr)
                {
                    canvas.DestroyObject(objects[i - 1]);
                }
            }
            canvas.FlushPendingDestroy();
            canvas.DestroyLayer(layer.GetId());
            return false;
        }
        created = layer.GetId();
        return true;
    }

}
