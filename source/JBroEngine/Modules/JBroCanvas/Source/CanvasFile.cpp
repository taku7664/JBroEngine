#include <JBro/Canvas/CanvasFile.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Reflection/ReflectedYaml.h>
#include <JBro/Runtime/GameObject.h>
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
    }

    bool WriteCanvasText(Canvas& canvas, String& text, CanvasFileError& error)
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

        YamlWriter writer;
        writer.WriteInt("Version", static_cast<std::int64_t>(CanvasFileVersion));

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
            writer.EndMap();
        }
        writer.EndSequence();

        writer.BeginSequence("Objects");
        for (std::size_t i = 0; i < ordered.Size(); ++i)
        {
            GameObject* object = ordered[i];
            // 오브젝트의 이름은 태그로 산다 — `Canvas::CreateObject(name)` 이 거기에 넣는다.
            error.objectName = object->GetTag();

            writer.BeginMap(nullptr);
            writer.WriteString("Name", object->GetTag());
            writer.WriteBool("Active", object->IsActiveSelf());

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
            layerOf.TryAdd(static_cast<std::uint64_t>(fileId), layer->GetId());
        }

        const std::uint32_t objects = document.Find(root, "Objects");
        Array<GameObject*> created;
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

}
