#include <JBro/Reflection/ReflectedYaml.h>

#include <JBro/Core/Yaml.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/TypeDescriptor.h>
#include <JBro/Types/NameTable.h>

#include <cstring>

namespace JBro
{
    namespace
    {
        bool Fail(ReflectedYamlError& error, const char* message)
        {
            error.message = message;
            return false;
        }

        bool ReadLeaf(
            const YamlDocument& document,
            std::uint32_t node,
            const TypeDescriptor& type,
            void* value,
            ReflectedYamlError& error)
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
            ReflectedYamlError& error)
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
    }

    // 코덱이 required 를 정확히 적어 주기로 되어 있으므로 두 번이면 끝난다.
    bool ReflectedValueToText(const ValueCodec& codec, const void* value, String& text)
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

    bool WriteReflectedValue(
        YamlWriter& writer,
        const char* key,
        const TypeDescriptor& type,
        const void* value,
        ReflectedYamlError& error)
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
                        || false == ReflectedValueToText(*field.type->codec,
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
                if (false == WriteReflectedValue(writer, NameTable::Get().Resolve(field.name),
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
        if (false == ReflectedValueToText(*type.codec, value, text))
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

    bool ReadReflectedFields(
        const YamlDocument& document,
        std::uint32_t node,
        const PropertyTable& table,
        void* value,
        const char* const* skip,
        std::size_t skipCount,
        ReflectedYamlError& error)
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
            if (false == ReadReflectedValue(document, document.GetValue(node, i),
                *property->type, property->Address(value), error))
            {
                return false;
            }
            error.fieldName.clear();
        }
        return true;
    }

    bool ReadReflectedValue(
        const YamlDocument& document,
        std::uint32_t node,
        const TypeDescriptor& type,
        void* value,
        ReflectedYamlError& error)
    {
        if (type.fields != nullptr)
        {
            if (type.writeFieldsAsSequence)
            {
                return ReadPacked(document, node, type, value, error);
            }
            return ReadReflectedFields(document, node, *type.fields, value, nullptr, 0, error);
        }
        if (type.codec == nullptr)
        {
            return Fail(error, "this value has neither fields nor a way to read itself");
        }
        return ReadLeaf(document, node, type, value, error);
    }
}
