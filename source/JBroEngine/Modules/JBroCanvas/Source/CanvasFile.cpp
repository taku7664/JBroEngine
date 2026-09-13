#include <JBro/Canvas/CanvasFile.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Table.h>

#include <cstdio>

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
            writer.WriteBool("IsEnabled", component->IsActiveComponent());
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
}
