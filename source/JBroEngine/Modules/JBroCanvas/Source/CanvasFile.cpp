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
#include <JBro/Types/Uuid.h>

#include <cstdio>
#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        constexpr UInt32 CanvasFileVersion = 1;
        // 레이어 에셋의 판이다. 캔버스 파일과 따로 센다(기존 엔진도 그랬다).
        constexpr UInt32 LayerFileVersion = 1;
        // 오브젝트 클립보드의 판이다(D-321). 맨 앞 키가 표식을 겸한다.
        constexpr UInt32 ObjectsTextVersion = 1;
        constexpr char ObjectsTextMarker[] = "ObjectsVersion:";

        Bool Fail(CanvasFileError& error, const char* message)
        {
            error.message = message;
            return false;
        }

        // 값의 걸음이 채운 오류를 파일 오류로 옮긴다. 오브젝트·타입 이름은 이쪽이 이미 적어 두었다.
        Bool FailFrom(CanvasFileError& error, const ReflectedYamlError& reflected)
        {
            error.message = reflected.message;
            error.fieldName = reflected.fieldName;
            return false;
        }

        // 컴포넌트나 스크립트 하나의 키들을 지금 자리에 적는다. 파일은 나열의 항목 안에, 핫 리로드는 맨 위에 적는다(D-300).
        // `instance` 는 그 타입의 객체다 - 프로퍼티 표의 자리는 그 타입 기준이다.
        Bool WriteFields(
            YamlWriter& writer,
            const void* instance,
            Bool enabled,
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
            for (UInt32 i = 0; i < table->count; ++i)
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

        Bool WriteComponent(YamlWriter& writer, const ComponentSlot& slot, CanvasFileError& error)
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

        Bool WriteScript(YamlWriter& writer, const GameScriptBase* script, ComponentTypeId typeId, CanvasFileError& error)
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
        Bool WriteUnresolvedScript(
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
        Bool ReadFields(const YamlDocument& document, UInt32 saved, ComponentTypeId typeId, void* instance,
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

        // **레이어 노드는 캔버스 파일과 레이어 에셋이 같은 함수로 쓰고 읽는다**(D-287). 둘이 갈리면 같은 레이어가 저장한 곳에 따라 다르게 읽힌다
        // (기존 엔진 `LayerSerializer.h` 의 경고). `inCanvas` 면 캔버스 안 번호(`Id`)와 원본 에셋(`SourceAsset`)도 적는다 - 레이어 에셋은 제 자신을
        // 가리키지 않는다. 기본값인 값은 적지 않는다(D-237·D-279·D-286).
        void WriteLayerNode(YamlWriter& writer, const Layer& layer, Bool inCanvas)
        {
            if (inCanvas)
            {
                writer.WriteInt("Id", static_cast<JBro::Int64>(layer.GetId()));
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
            // 빛을 받는 것이 기본이다(D-291). 끈 레이어만 적는다.
            if (false == layer.IsLit())
            {
                writer.WriteBool("Lit", false);
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
            Bool visible = true;
            LayerSpace space = LayerSpace::World;
            ScreenScaleMode scaleMode = ScreenScaleMode::FixedHeight;
            LayerBlend blend = LayerBlend::Normal;
            Float opacity = 1.0f;
            Float parallax = 1.0f;
            Bool lit = true;
            Uuid sourceAsset;
        };

        Bool ReadLayerNode(const YamlDocument& document, UInt32 entry, LayerNodeValues& values, CanvasFileError& error)
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
            document.FindBool(entry, "Lit", values.lit);
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
            layer.SetLit(values.lit);
            layer.SetSourceAsset(values.sourceAsset);
        }

        // 오브젝트 목록을 적는다. `ordered` 는 부모가 자식보다 앞이다. `writeLayer` 면 오브젝트마다 레이어 번호를 적는다(캔버스 파일) - 레이어 에셋은
        // 오브젝트가 모두 그 레이어라 적지 않는다.
        // `outsideParentIsRoot` 면 목록 밖의 부모를 실패 대신 뿌리로 적는다(오브젝트 클립보드 - 고른 나무의 부모는 글자 밖이다).
        Bool WriteObjects(Canvas& canvas, YamlWriter& writer, const Array<Object::GameObject*>& ordered, Table<const Object::GameObject*, std::size_t>& indexOf,
            CanvasWriteMode mode, Bool writeLayer, CanvasFileError& error, Bool outsideParentIsRoot = false)
        {
            Array<ScriptEntry> scriptEntries;
            writer.BeginSequence("Objects");
            for (std::size_t i = 0; i < ordered.Size(); ++i)
            {
                Object::GameObject* object = ordered[i];
                // 오브젝트의 이름이다 — `Canvas::CreateObject(name)` 이 거기에 넣는다.
                error.objectName = object->GetName();

                writer.BeginMap(nullptr);
                writer.WriteString("Name", object->GetName());
                // 태그는 있을 때만 적는다(D-297). 없으면 태그가 없는 것으로 읽는다.
                if (object->GetTagId() != InvalidNameId)
                {
                    writer.WriteString("Tag", object->GetTag());
                }
                writer.WriteBool("Active", object->IsActiveSelf());
                // 플래그는 있을 때만 적는다 - 대부분의 오브젝트는 0 이고, 없으면 0 으로 읽는다.
                const UInt32 flags = mode == CanvasWriteMode::Package
                    ? (object->GetFlags() & ~EditorOnlyObjectFlags)
                    : object->GetFlags();
                if (flags != 0)
                {
                    writer.WriteInt("Flags", static_cast<JBro::Int64>(flags));
                }

                Int64 parentIndex = -1;
                if (object->GetParent() != nullptr)
                {
                    const std::size_t* found = indexOf.Find(object->GetParent());
                    if (found == nullptr && false == outsideParentIsRoot)
                    {
                        return Fail(error, "an object hangs from something that is not in this canvas");
                    }
                    parentIndex = found != nullptr ? static_cast<JBro::Int64>(*found) : Int64(-1);
                }
                writer.WriteInt("ParentIndex", parentIndex);
                if (writeLayer)
                {
                    writer.WriteInt("LayerId", static_cast<JBro::Int64>(object->GetLayerId()));
                }

                writer.BeginSequence("Components");
                for (const ComponentSlot& slot : object->GetComponents())
                {
                    if (false == WriteComponent(writer, slot, error))
                    {
                        return false;
                    }
                }
                writer.EndSequence();

                // **스크립트는 따로 적는다**(D-302). 없으면 키를 적지 않는다 - 스크립트가 없는 오브젝트가 대부분이다.
                // 모르는 스크립트(D-264)는 읽을 때 앞에 있던 스크립트 개수 자리에 끼운다.
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
            return true;
        }

        // 오브젝트가 어느 레이어에 서는가. 캔버스 파일은 파일의 레이어 번호를 짝지은 표로, 레이어 에셋은 새로 세운 레이어 하나로 정한다.
        struct ObjectLayerRule
        {
            const Table<UInt64, LayerId>* layerOf = nullptr;
            LayerId fixedLayer = InvalidLayerId;
            // 표에 없는 레이어를 실패 대신 기본 레이어로 둔다(오브젝트 클립보드 - 복사한 레이어가 사라졌거나 다른 캔버스다).
            Bool unknownToDefault = false;
            // 다른 프로세스가 쓴 글자의 `@` 번호를 버린다(D-321).
            Bool dropInstanceRefs = false;
        };

        // 오브젝트를 만든다. **모두 만든 뒤 컴포넌트를 읽는다**(D-233) - 오브젝트 참조 필드는 뒤에 오는 오브젝트도 가리킬 수 있다. 실패해도 만든 것은
        // `created` 에 남는다 - 거두는 것은 부르는 쪽이다.
        Bool ReadObjects(Canvas& canvas, const YamlDocument& document, UInt32 objects, const ObjectLayerRule& rule,
            Array<Object::GameObject*>& created, CanvasFileError& error)
        {
            for (std::size_t i = 0; i < document.GetCount(objects); ++i)
            {
                const UInt32 entry = document.GetElement(objects, i);
                String name;
                document.FindScalar(entry, "Name", name);
                error.objectName = name;

                Object::GameObject* object = canvas.CreateObject(name.c_str());
                if (object == nullptr)
                {
                    return Fail(error, "an object in this file could not be created");
                }
                created.Add(object);
                String tag;
                if (document.FindScalar(entry, "Tag", tag))
                {
                    object->SetTag(tag.c_str());
                }
                Int64 flags = 0;
                if (document.FindInt(entry, "Flags", flags))
                {
                    if (flags < 0 || flags > static_cast<JBro::Int64>(UINT32_MAX))
                    {
                        return Fail(error, "an object in this file has flags that do not fit");
                    }
                    object->SetFlags(static_cast<JBro::UInt32>(flags));
                }

                Int64 parentIndex = -1;
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
                    Int64 fileLayer = 0;
                    if (rule.layerOf != nullptr && document.FindInt(entry, "LayerId", fileLayer))
                    {
                        const LayerId* mapped = rule.layerOf->Find(static_cast<JBro::UInt64>(fileLayer));
                        if (mapped == nullptr && false == rule.unknownToDefault)
                        {
                            return Fail(error, "an object sits on a layer this file never described");
                        }
                        canvas.SetObjectLayer(object, mapped != nullptr ? *mapped : canvas.GetDefaultLayer());
                    }
                }
            }

            Internal::ObjectRefRemap remap;
            remap.dropInstanceRefs = rule.dropInstanceRefs;
            remap.user = &created;
            remap.toObjectId = [](void* user, Int64 index) -> InstanceId {
                const Array<Object::GameObject*>& objects = *static_cast<Array<Object::GameObject*>*>(user);
                return index >= 0 && static_cast<std::size_t>(index) < objects.Size()
                    ? objects[static_cast<std::size_t>(index)]->GetInstanceId()
                    : InvalidInstanceId;
            };
            ObjectRefRemapScope remapScope(remap);
            for (std::size_t i = 0; i < created.Size(); ++i)
            {
                const UInt32 entry = document.GetElement(objects, i);
                Object::GameObject* object = created[i];
                error.objectName = object->GetName();

                const UInt32 components = document.Find(entry, "Components");
                for (std::size_t c = 0; c < document.GetCount(components); ++c)
                {
                    const UInt32 saved = document.GetElement(components, c);
                    String typeName;
                    if (false == document.FindScalar(saved, "Type", typeName))
                    {
                        return Fail(error, "a component in this file does not say what it is");
                    }
                    error.typeName = typeName;

                    const ComponentTypeInfo* info = ComponentRegistry::Get().Find(MakeNameId(typeName.c_str()));
                    if (info == nullptr)
                    {
                        // **스크립트는 `Scripts` 에 있어야 한다**(D-302). 둘이 한 목록이던 파일은 옮겨 읽지 않는다(사용자 결정 2026-09-29).
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
                    Bool enabled = true;
                    if (document.FindBool(saved, "IsEnabled", enabled))
                    {
                        component->SetEnabled(enabled);
                    }
                    error.typeName.clear();
                }

                const UInt32 scripts = document.Find(entry, "Scripts");
                UInt32 resolvedCount = 0;
                for (std::size_t s = 0; s < document.GetCount(scripts); ++s)
                {
                    const UInt32 saved = document.GetElement(scripts, s);
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
                        UnresolvedScript keptScript;
                        keptScript.typeName = typeName;
                        keptScript.text = fragment.GetText();
                        keptScript.position = resolvedCount;
                        if (false == canvas.AddUnresolvedScript(object, std::move(keptScript)))
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
                    Bool enabled = true;
                    if (document.FindBool(saved, "IsEnabled", enabled))
                    {
                        script->SetEnabled(enabled);
                    }
                    error.typeName.clear();
                }

                // 활성은 마지막이다. 부모가 정해진 뒤라야 상속 활성값이 맞는다.
                Bool active = true;
                document.FindBool(entry, "Active", active);
                object->SetActive(active);
            }
            error.objectName.clear();
            return true;
        }

        // 오브젝트 참조를 파일 안 번호로 적는 표다(D-233). 오브젝트 번호는 실행마다 달라 파일에 남길 수 없다. 목록 밖을 가리키면 -1 이다.
        struct FileIndexMap
        {
            explicit FileIndexMap(const Array<Object::GameObject*>& ordered)
            {
                for (std::size_t i = 0; i < ordered.Size(); ++i)
                {
                    fileIndexOf.TryAdd(ordered[i]->GetInstanceId(), static_cast<JBro::Int64>(i));
                }
                remap.user = &fileIndexOf;
                remap.toIndex = [](void* user, InstanceId objectId) -> Int64 {
                    const Int64* found = static_cast<Table<InstanceId, Int64>*>(user)->Find(objectId);
                    return found != nullptr ? *found : Int64(-1);
                };
            }

            Table<InstanceId, Int64> fileIndexOf;
            Internal::ObjectRefRemap remap;
        };
    }

    Bool WriteCanvasText(Canvas& canvas, String& text, CanvasFileError& error, CanvasWriteMode mode)
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

        FileIndexMap fileIndex(ordered);
        ObjectRefRemapScope remapScope(fileIndex.remap);

        YamlWriter writer;
        writer.WriteInt("Version", static_cast<JBro::Int64>(CanvasFileVersion));

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

        if (false == WriteObjects(canvas, writer, ordered, indexOf, mode, true, error))
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

    Bool ReadCanvasText(Canvas& canvas, const char* text, std::size_t length, CanvasFileError& error)
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

        const UInt32 root = document.GetRoot();
        Int64 version = 0;
        if (false == document.FindInt(root, "Version", version))
        {
            return Fail(error, "this file does not say what version it is");
        }
        if (version != static_cast<JBro::Int64>(CanvasFileVersion))
        {
            return Fail(error, "this file was written by a different version of the format");
        }

        // 배경색은 **없어도 된다**(D-186). 이 키가 생기기 전의 파일은 캔버스의 기본값으로
        // 열린다 - 그것이 그때 화면에 나오던 색이다.
        {
            const UInt32 background = document.Find(root, "BackgroundColor");
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
        Table<UInt64, LayerId> layerOf;
        const UInt32 layers = document.Find(root, "Layers");
        Bool firstLayer = true;
        for (std::size_t i = 0; i < document.GetCount(layers); ++i)
        {
            const UInt32 entry = document.GetElement(layers, i);
            LayerNodeValues values;
            if (false == ReadLayerNode(document, entry, values, error))
            {
                return false;
            }
            Int64 fileId = 0;
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
            layerOf.TryAdd(static_cast<JBro::UInt64>(fileId), layer->GetId());
        }

        Array<Object::GameObject*> created;
        ObjectLayerRule rule;
        rule.layerOf = &layerOf;
        if (false == ReadObjects(canvas, document, document.Find(root, "Objects"), rule, created, error))
        {
            return false;
        }

        // 모르는 컴포넌트가 들고 있는 파일 안 번호를 뒤에 풀 수 있게 이 파일의 차례를 캔버스에 둔다(D-300).
        {
            Array<InstanceId> order;
            order.Reserve(created.Size());
            for (Object::GameObject* object : created)
            {
                order.Add(object->GetInstanceId());
            }
            canvas.SetFileObjectOrder(std::move(order));
        }
        error.objectName.clear();
        return true;
    }

    // -----------------------------------------------------------------------
    // 레이어 에셋(`.jlayer`, D-287)
    // -----------------------------------------------------------------------

    Bool WriteLayerText(Canvas& canvas, LayerId layerId, String& text, CanvasFileError& error)
    {
        error = CanvasFileError{};
        const Layer* layer = canvas.FindLayer(layerId);
        if (layer == nullptr)
        {
            return Fail(error, "there is no such layer in this canvas");
        }
        // 이 레이어의 뿌리를 캔버스의 보이는 순서대로, 자식을 그 뒤에. 부분 트리는 늘 한 레이어다(D-135).
        Array<Object::GameObject*> roots;
        canvas.GetRootObjects(roots);
        Array<Object::GameObject*> ordered;
        for (std::size_t i = 0; i < roots.Size(); ++i)
        {
            if (roots[i]->GetLayerId() == layerId)
            {
                CollectInOrder(roots[i], ordered);
            }
        }
        Table<const Object::GameObject*, std::size_t> indexOf;
        for (std::size_t i = 0; i < ordered.Size(); ++i)
        {
            indexOf.TryAdd(ordered[i], i);
        }
        // 레이어 밖을 가리키는 오브젝트 참조는 -1(비어 있음)로 적힌다 - 그 오브젝트는 이 파일에 없다.
        FileIndexMap fileIndex(ordered);
        ObjectRefRemapScope remapScope(fileIndex.remap);

        YamlWriter writer;
        writer.WriteInt("Version", static_cast<JBro::Int64>(LayerFileVersion));
        writer.BeginMap("Layer");
        WriteLayerNode(writer, *layer, false);
        writer.EndMap();
        if (false == WriteObjects(canvas, writer, ordered, indexOf, CanvasWriteMode::Editor, false, error))
        {
            return false;
        }
        text = writer.GetText();
        return true;
    }

    Bool ReadLayerText(Canvas& canvas, const char* text, std::size_t length, LayerId& created, CanvasFileError& error)
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
        const UInt32 root = document.GetRoot();
        Int64 version = 0;
        if (false == document.FindInt(root, "Version", version))
        {
            return Fail(error, "this file does not say what version it is");
        }
        if (version != static_cast<JBro::Int64>(LayerFileVersion))
        {
            return Fail(error, "this file was written by a different version of the format");
        }
        const UInt32 node = document.Find(root, "Layer");
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
        Array<Object::GameObject*> objects;
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

    Bool IsLayerText(const char* text)
    {
        // 레이어 에셋은 `Version` 다음이 `Layer` 다(`WriteLayerText`). 캔버스 파일은 `Version` 다음이 `Layers` 다.
        if (text == nullptr || std::strncmp(text, "Version:", 8) != 0)
        {
            return false;
        }
        const char* next = std::strchr(text, '\n');
        return next != nullptr && std::strncmp(next + 1, "Layer:", 6) == 0;
    }

    // -----------------------------------------------------------------------
    // 오브젝트 클립보드(D-321)
    // -----------------------------------------------------------------------

    Bool IsObjectsText(const char* text)
    {
        return text != nullptr && std::strncmp(text, ObjectsTextMarker, sizeof(ObjectsTextMarker) - 1) == 0;
    }

    Bool WriteObjectsText(Canvas& canvas, const Array<Object::GameObject*>& roots, const char* session, String& text, CanvasFileError& error)
    {
        error = CanvasFileError{};
        if (roots.IsEmpty())
        {
            return Fail(error, "there is nothing to copy");
        }
        Array<Object::GameObject*> ordered;
        for (std::size_t i = 0; i < roots.Size(); ++i)
        {
            if (roots[i] == nullptr)
            {
                return Fail(error, "an object to copy is gone");
            }
            CollectInOrder(roots[i], ordered);
        }
        Table<const Object::GameObject*, std::size_t> indexOf;
        for (std::size_t i = 0; i < ordered.Size(); ++i)
        {
            indexOf.TryAdd(ordered[i], i);
        }
        FileIndexMap fileIndex(ordered);
        fileIndex.remap.outsideAsInstance = true;
        ObjectRefRemapScope remapScope(fileIndex.remap);

        YamlWriter writer;
        writer.WriteInt("ObjectsVersion", static_cast<JBro::Int64>(ObjectsTextVersion));
        writer.WriteString("Session", session != nullptr ? session : "");
        // 뿌리의 부모는 글자 밖이다. 글자 안에서는 뿌리로 적고, 붙이는 쪽이 자리를 정한다.
        if (false == WriteObjects(canvas, writer, ordered, indexOf, CanvasWriteMode::Editor, true, error, true))
        {
            return false;
        }
        text = writer.GetText();
        return true;
    }

    Bool ReadObjectsText(Canvas& canvas, const char* text, std::size_t length, const ObjectsTextTarget& target,
        Array<Object::GameObject*>& roots, CanvasFileError& error)
    {
        error = CanvasFileError{};
        roots.Clear();
        YamlDocument document;
        YamlError parseError;
        if (false == document.Parse(text, length, parseError))
        {
            error.message = parseError.message;
            return false;
        }
        const UInt32 root = document.GetRoot();
        Int64 version = 0;
        if (false == document.FindInt(root, "ObjectsVersion", version))
        {
            return Fail(error, "this text holds no objects");
        }
        if (version != static_cast<JBro::Int64>(ObjectsTextVersion))
        {
            return Fail(error, "this text was written by a different version of the format");
        }
        String session;
        document.FindScalar(root, "Session", session);

        ObjectLayerRule rule;
        Table<UInt64, LayerId> layerOf;
        if (target.layer != InvalidLayerId && canvas.FindLayer(target.layer) != nullptr)
        {
            rule.fixedLayer = target.layer;
        }
        else if (target.parent != nullptr)
        {
            // 부분 트리는 늘 한 레이어다(D-135). 부모의 레이어를 따른다.
            rule.fixedLayer = target.parent->GetLayerId();
        }
        else
        {
            for (std::size_t i = 0; i < canvas.GetLayerCount(); ++i)
            {
                const LayerId id = canvas.GetLayerAt(i)->GetId();
                layerOf.TryAdd(static_cast<JBro::UInt64>(id), id);
            }
            rule.layerOf = &layerOf;
            rule.unknownToDefault = true;
        }
        rule.dropInstanceRefs = target.session == nullptr || session.empty() || session != target.session;

        Array<Object::GameObject*> created;
        if (false == ReadObjects(canvas, document, document.Find(root, "Objects"), rule, created, error))
        {
            // **반쯤 들어온 것을 남기지 않는다.** 레이어 에셋 읽기와 같다.
            for (std::size_t i = created.Size(); i > 0; --i)
            {
                if (created[i - 1]->GetParent() == nullptr)
                {
                    canvas.DestroyObject(created[i - 1]);
                }
            }
            canvas.FlushPendingDestroy();
            return false;
        }
        for (std::size_t i = 0; i < created.Size(); ++i)
        {
            if (created[i]->GetParent() == nullptr)
            {
                roots.Add(created[i]);
                continue;
            }
            // 부분 트리는 늘 한 레이어다(D-135). 손으로 고친 글자가 자식을 다른 레이어에 적어도 뿌리를 따른다. 부모가 늘 앞에 있다.
            canvas.SetObjectLayer(created[i], created[i]->GetParent()->GetLayerId());
        }
        if (roots.IsEmpty())
        {
            return Fail(error, "this text holds no objects");
        }
        if (target.parent != nullptr)
        {
            for (std::size_t i = 0; i < roots.Size(); ++i)
            {
                roots[i]->SetParent(target.parent);
            }
        }
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
            for (UInt32 index = 0; index < table.count; ++index)
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

    Bool KeepScriptsAsText(Canvas& canvas, std::size_t& kept, CanvasFileError& error)
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
            error.objectName = object->GetName();
            OrderScripts(*object, canvas.FindUnresolvedScripts(object), entries);
            Bool changes = false;
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
            UInt32 resolvedBefore = 0;
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
        remap.toObjectId = [](void* user, Int64 index) -> InstanceId {
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
            Bool changed = false;
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
                const Bool known = type != nullptr
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
                        note.objectName = object->GetName();
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
                    copy.position = static_cast<UInt32>(resolvedBefore);
                    remaining.Add(std::move(copy));
                    continue;
                }

                const UInt32 root = document.GetRoot();
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
                    note.objectName = object->GetName();
                    note.typeName = kept.typeName;
                    note.fieldName = fieldName;
                    notes.Add(std::move(note));
                }
                Bool enabled = true;
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
