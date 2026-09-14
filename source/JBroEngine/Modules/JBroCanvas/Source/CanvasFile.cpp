#include <JBro/Canvas/CanvasFile.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Reflection/PropertyRegistry.h>
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

        // 코덱이 내놓는 글자를 받아 온다. 버퍼가 모자라면 필요한 만큼 잡고 한 번 더 묻는다 —
        // 코덱이 required 를 정확히 적어 주기로 되어 있으므로 두 번이면 끝난다.
        bool ValueToText(const ValueCodec& codec, const void* value, String& text)
        {
            if (codec.ToText == nullptr)
            {
                return false;
            }
            char stack[128];
            std::size_t required = 0;
            if (codec.ToText(value, stack, sizeof(stack), required))
            {
                text.assign(stack, required);
                return true;
            }
            if (required == 0 || required > (1u << 20))
            {
                return false;
            }
            text.resize(required);
            std::size_t again = 0;
            if (false == codec.ToText(value, &text[0], text.size(), again) || again != required)
            {
                return false;
            }
            return true;
        }

        // 값 하나를 적는다. 필드가 있으면 타고 내려가고, 없으면 코덱으로 글자를 얻는다.
        // key 가 nullptr 이면 시퀀스 항목 자리다.
        bool WriteValue(
            YamlWriter& writer,
            const char* key,
            const TypeDescriptor& type,
            const void* value,
            CanvasFileError& error)
        {
            if (type.fields != nullptr)
            {
                if (type.writeFieldsAsSequence)
                {
                    if (key == nullptr)
                    {
                        return Fail(error, "a packed value cannot sit inside another packed value");
                    }
                    writer.BeginSequence(key);
                    for (std::uint32_t i = 0; i < type.fields->count; ++i)
                    {
                        const PropertyInfo& field = type.fields->properties[i];
                        String text;
                        if (field.type == nullptr || field.type->codec == nullptr
                            || false == ValueToText(*field.type->codec,
                                field.ConstAddress(value), text))
                        {
                            error.fieldName = NameTable::Get().Resolve(field.name);
                            return Fail(error,
                                "a type written as a plain list must hold values that can be written");
                        }
                        writer.WriteStringItem(text.c_str());
                    }
                    writer.EndSequence();
                    return true;
                }

                writer.BeginMap(key);
                for (std::uint32_t i = 0; i < type.fields->count; ++i)
                {
                    const PropertyInfo& field = type.fields->properties[i];
                    if (false == field.serialize || field.type == nullptr)
                    {
                        continue;
                    }
                    if (false == WriteValue(writer, NameTable::Get().Resolve(field.name),
                        *field.type, field.ConstAddress(value), error))
                    {
                        return false;
                    }
                }
                writer.EndMap();
                return true;
            }

            if (type.codec == nullptr)
            {
                return Fail(error, "this value has neither fields nor a way to write itself");
            }
            String text;
            if (false == ValueToText(*type.codec, value, text))
            {
                return Fail(error, "this value refused to be written");
            }
            if (key == nullptr)
            {
                writer.WriteStringItem(text.c_str());
            }
            else
            {
                writer.WriteString(key, text.c_str());
            }
            return true;
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
                if (false == WriteValue(writer, NameTable::Get().Resolve(property.name),
                    *property.type, property.ConstAddress(component), error))
                {
                    return false;
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
        Array<GameObject*> roots;
        canvas.ForEachObject([&roots](GameObject& object)
        {
            if (object.GetParent() == nullptr)
            {
                roots.Add(&object);
            }
        });

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

    bool SaveCanvasFile(Canvas& canvas, const char* path, CanvasFileError& error)
    {
        String text;
        if (false == WriteCanvasText(canvas, text, error))
        {
            return false;
        }
        std::FILE* file = nullptr;
        if (path == nullptr || path[0] == '\0')
        {
            return Fail(error, "no path was given");
        }
        if (fopen_s(&file, path, "wb") != 0 || file == nullptr)
        {
            return Fail(error, "cannot open the file for writing");
        }
        const std::size_t written = text.empty()
            ? 0 : std::fwrite(text.c_str(), 1, text.size(), file);
        std::fclose(file);
        if (written != text.size())
        {
            return Fail(error, "the file was not written in full");
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // 읽기
    // -----------------------------------------------------------------------

    namespace
    {
        bool ReadValue(
            const YamlDocument& document,
            std::uint32_t node,
            const TypeDescriptor& type,
            void* value,
            CanvasFileError& error);

        bool ReadLeaf(
            const YamlDocument& document,
            std::uint32_t node,
            const TypeDescriptor& type,
            void* value,
            CanvasFileError& error)
        {
            if (document.GetKind(node) != YamlKind::Scalar)
            {
                return Fail(error, "a plain value was expected here");
            }
            const char* text = document.GetText(node);
            if (false == type.codec->FromText(value, text, std::strlen(text)))
            {
                // 읽히지 않는 값을 기본값으로 대신하지 않는다. 씬이 조용히 달라진다.
                return Fail(error, "this value could not be read back");
            }
            return true;
        }

        bool ReadPacked(
            const YamlDocument& document,
            std::uint32_t node,
            const TypeDescriptor& type,
            void* value,
            CanvasFileError& error)
        {
            if (document.GetKind(node) != YamlKind::Sequence)
            {
                return Fail(error, "a list was expected here");
            }
            // 개수가 다르면 어느 자리가 어느 필드인지 알 수 없다. 나열은 순서가 전부다.
            if (document.GetCount(node) != type.fields->count)
            {
                return Fail(error, "this list does not have one entry per member");
            }
            for (std::uint32_t i = 0; i < type.fields->count; ++i)
            {
                const PropertyInfo& field = type.fields->properties[i];
                error.fieldName = NameTable::Get().Resolve(field.name);
                if (field.type == nullptr || field.type->codec == nullptr)
                {
                    return Fail(error, "a member of a packed value cannot be read");
                }
                if (false == ReadLeaf(document, document.GetElement(node, i),
                    *field.type, field.Address(value), error))
                {
                    return false;
                }
            }
            error.fieldName.clear();
            return true;
        }

        // 맵 하나의 키들을 프로퍼티 표에 맞춰 읽는다. skip 에 있는 키는 표에 없어도 넘어간다
        // (컴포넌트의 Type·IsEnabled 처럼 표가 아니라 파일 형식이 정한 키다).
        bool ReadFieldsFromMap(
            const YamlDocument& document,
            std::uint32_t node,
            const PropertyTable& table,
            void* value,
            const char* const* skip,
            std::size_t skipCount,
            CanvasFileError& error)
        {
            if (document.GetKind(node) != YamlKind::Map)
            {
                return Fail(error, "a block of named values was expected here");
            }
            for (std::size_t i = 0; i < document.GetCount(node); ++i)
            {
                const char* key = document.GetKey(node, i);
                bool skipped = false;
                for (std::size_t s = 0; s < skipCount; ++s)
                {
                    if (std::strcmp(key, skip[s]) == 0)
                    {
                        skipped = true;
                        break;
                    }
                }
                if (skipped)
                {
                    continue;
                }

                const NameId name = MakeNameId(key);
                const PropertyInfo* property = nullptr;
                for (std::uint32_t p = 0; p < table.count; ++p)
                {
                    if (table.properties[p].name == name)
                    {
                        property = &table.properties[p];
                        break;
                    }
                }
                if (property == nullptr || false == property->serialize)
                {
                    // 파일에 있는데 코드에 없는 필드다. 조용히 버리면 그 씬이 들고 있던
                    // 값이 사라지고 아무도 모른다.
                    error.fieldName = key;
                    return Fail(error, "this file has a field the engine no longer knows");
                }
                error.fieldName = key;
                if (property->type == nullptr)
                {
                    return Fail(error, "this field names no type");
                }
                if (false == ReadValue(document, document.GetValue(node, i),
                    *property->type, property->Address(value), error))
                {
                    return false;
                }
                error.fieldName.clear();
            }
            return true;
        }

        bool ReadValue(
            const YamlDocument& document,
            std::uint32_t node,
            const TypeDescriptor& type,
            void* value,
            CanvasFileError& error)
        {
            if (type.fields != nullptr)
            {
                if (type.writeFieldsAsSequence)
                {
                    return ReadPacked(document, node, type, value, error);
                }
                return ReadFieldsFromMap(document, node, *type.fields, value, nullptr, 0, error);
            }
            if (type.codec == nullptr)
            {
                return Fail(error, "this value has neither fields nor a way to read itself");
            }
            return ReadLeaf(document, node, type, value, error);
        }
    }

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
                if (false == ReadFieldsFromMap(document, saved, *table, component,
                    formatKeys, sizeof(formatKeys) / sizeof(formatKeys[0]), error))
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

            // 활성은 마지막이다. 부모가 정해진 뒤라야 상속 활성값이 맞는다.
            bool active = true;
            document.FindBool(entry, "Active", active);
            object->SetActive(active);
        }

        error.objectName.clear();
        return true;
    }

    bool LoadCanvasFile(Canvas& canvas, const char* path, CanvasFileError& error)
    {
        error = CanvasFileError{};
        if (path == nullptr || path[0] == 0)
        {
            return Fail(error, "no path was given");
        }
        std::FILE* file = nullptr;
        if (fopen_s(&file, path, "rb") != 0 || file == nullptr)
        {
            return Fail(error, "cannot open the file");
        }
        String text;
        char buffer[4096];
        std::size_t read = 0;
        while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        {
            text.append(buffer, read);
        }
        std::fclose(file);
        return ReadCanvasText(canvas, text.c_str(), text.size(), error);
    }
}
