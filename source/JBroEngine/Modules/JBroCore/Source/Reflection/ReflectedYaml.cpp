#include <JBro/Reflection/ReflectedYaml.h>

#include <JBro/Core/Yaml.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/TypeDescriptor.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/NameTable.h>

#include <algorithm>
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

        // 표의 키 하나를 타입을 모른 채 만들고, 어떻게 끝나든 돌려준다.
        struct ScopedKey
        {
            const TableOps& ops;
            void* key = nullptr;

            explicit ScopedKey(const TableOps& tableOps)
                : ops(tableOps)
                , key(tableOps.CreateKey != nullptr ? tableOps.CreateKey() : nullptr)
            {
            }
            ~ScopedKey()
            {
                if (key != nullptr && ops.DestroyKey != nullptr)
                {
                    ops.DestroyKey(key);
                }
            }
            ScopedKey(const ScopedKey&) = delete;
            ScopedKey& operator=(const ScopedKey&) = delete;
        };

        bool WriteArray(
            YamlWriter& writer,
            const char* key,
            const TypeDescriptor& type,
            const void* value,
            ReflectedYamlError& error)
        {
            const ArrayOps& ops = *type.arrayOps;
            if (type.element == nullptr || ops.GetSize == nullptr || ops.GetConstElement == nullptr)
            {
                return Fail(error, "this list cannot be walked");
            }
            writer.BeginSequence(key);
            const std::size_t size = ops.GetSize(value);
            for (std::size_t index = 0; index < size; ++index)
            {
                const void* element = ops.GetConstElement(value, index);
                if (element == nullptr)
                {
                    return Fail(error, "a list element went missing while it was written");
                }
                if (false == WriteReflectedValue(writer, nullptr, *type.element, element, error))
                {
                    return false;
                }
            }
            writer.EndSequence();
            return true;
        }

        // **키 글자 순으로 적는다.** 슬롯 순서는 넣은 내력에 따라 달라지므로 그대로 적으면
        // 내용이 같은 두 표가 다른 글자가 된다 - 되돌리기가 바뀌지 않은 것을 바뀌었다고 보고,
        // 저장 파일도 괜히 달라진다.
        bool WriteTable(
            YamlWriter& writer,
            const char* key,
            const TypeDescriptor& type,
            const void* value,
            ReflectedYamlError& error)
        {
            const TableOps& ops = *type.tableOps;
            if (type.key == nullptr || type.value == nullptr || ops.BeginSlot == nullptr
                || ops.NextSlot == nullptr || ops.GetKeyAt == nullptr || ops.GetValueAt == nullptr)
            {
                return Fail(error, "this table cannot be walked");
            }
            if (type.key->codec == nullptr)
            {
                // 키는 한 줄 글자여야 한다. 구조를 가진 키는 무엇으로 정렬하고 무엇으로
                // 같다고 볼지부터 정해야 하는데, 그런 키를 쓰는 필드가 아직 없다.
                return Fail(error, "a table key must be a plain value");
            }

            struct Entry
            {
                String text;
                std::size_t slot = TableOps::InvalidSlot;
            };
            Array<Entry> entries;
            for (std::size_t slot = ops.BeginSlot(value); slot != TableOps::InvalidSlot;
                slot = ops.NextSlot(value, slot))
            {
                Entry entry;
                entry.slot = slot;
                if (false == ReflectedValueToText(*type.key->codec, ops.GetKeyAt(value, slot),
                    entry.text))
                {
                    return Fail(error, "a table key refused to be written");
                }
                entries.Add(std::move(entry));
            }
            std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
                return std::strcmp(left.text.c_str(), right.text.c_str()) < 0;
            });

            writer.BeginSequence(key);
            for (std::size_t index = 0; index < entries.Size(); ++index)
            {
                writer.BeginMap(nullptr);
                writer.WriteString("Key", entries[index].text.c_str());
                // 조작 함수가 값 주소를 쓰기 가능으로만 내준다. 여기서는 읽기만 한다.
                const void* entryValue =
                    ops.GetValueAt(const_cast<void*>(value), entries[index].slot);
                if (entryValue == nullptr)
                {
                    return Fail(error, "a table value went missing while it was written");
                }
                if (false == WriteReflectedValue(writer, "Value", *type.value, entryValue, error))
                {
                    return false;
                }
                writer.EndMap();
            }
            writer.EndSequence();
            return true;
        }

        // **있던 원소를 버리고 파일의 것으로 채운다.** 되돌리기가 원소 수까지 되살려야 한다.
        bool ReadArray(
            const YamlDocument& document,
            std::uint32_t node,
            const TypeDescriptor& type,
            void* value,
            ReflectedYamlError& error)
        {
            const ArrayOps& ops = *type.arrayOps;
            if (type.element == nullptr || ops.Clear == nullptr || ops.AddDefault == nullptr
                || ops.GetSize == nullptr || ops.GetElement == nullptr)
            {
                return Fail(error, "this list cannot be walked");
            }
            if (document.GetKind(node) != YamlKind::Sequence)
            {
                return Fail(error, "a list was expected here");
            }
            ops.Clear(value);
            for (std::size_t index = 0; index < document.GetCount(node); ++index)
            {
                if (false == ops.AddDefault(value))
                {
                    return Fail(error, "the list refused another element");
                }
                void* element = ops.GetElement(value, ops.GetSize(value) - 1);
                if (element == nullptr)
                {
                    return Fail(error, "a list element went missing while it was read");
                }
                if (false == ReadReflectedValue(document, document.GetElement(node, index),
                    *type.element, element, error))
                {
                    return false;
                }
            }
            return true;
        }

        bool ReadTable(
            const YamlDocument& document,
            std::uint32_t node,
            const TypeDescriptor& type,
            void* value,
            ReflectedYamlError& error)
        {
            const TableOps& ops = *type.tableOps;
            if (type.key == nullptr || type.value == nullptr || type.key->codec == nullptr
                || type.key->codec->FromText == nullptr || ops.Clear == nullptr
                || ops.CreateKey == nullptr || ops.DestroyKey == nullptr
                || ops.ContainsKey == nullptr || ops.InsertDefault == nullptr
                || ops.FindValue == nullptr)
            {
                return Fail(error, "this table cannot be walked");
            }
            if (document.GetKind(node) != YamlKind::Sequence)
            {
                return Fail(error, "a list of table entries was expected here");
            }
            ops.Clear(value);
            for (std::size_t index = 0; index < document.GetCount(node); ++index)
            {
                const std::uint32_t entry = document.GetElement(node, index);
                String keyText;
                const std::uint32_t valueNode = document.GetKind(entry) == YamlKind::Map
                    ? document.Find(entry, "Value") : YamlDocument::InvalidNode;
                if (valueNode == YamlDocument::InvalidNode
                    || false == document.FindScalar(entry, "Key", keyText)
                    || document.GetCount(entry) != 2)
                {
                    return Fail(error, "a table entry must hold exactly a Key and a Value");
                }

                ScopedKey key(ops);
                if (key.key == nullptr
                    || false == type.key->codec->FromText(key.key, keyText.c_str(), keyText.size()))
                {
                    return Fail(error, "a table key could not be read back");
                }
                // 같은 키가 두 번이면 뒤의 것이 앞의 값을 조용히 덮는다. 실패로 둔다.
                if (ops.ContainsKey(value, key.key))
                {
                    return Fail(error, "this table names the same key twice");
                }
                if (false == ops.InsertDefault(value, key.key))
                {
                    return Fail(error, "the table refused another entry");
                }
                void* entryValue = ops.FindValue(value, key.key);
                if (entryValue == nullptr)
                {
                    return Fail(error, "a table value went missing while it was read");
                }
                if (false == ReadReflectedValue(document, valueNode, *type.value, entryValue, error))
                {
                    return false;
                }
            }
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
        if (type.arrayOps != nullptr)
        {
            return WriteArray(writer, key, type, value, error);
        }
        if (type.tableOps != nullptr)
        {
            return WriteTable(writer, key, type, value, error);
        }
        if (type.fields != nullptr)
        {
            if (type.writeFieldsAsSequence)
            {
                // 시퀀스 항목 자리면(배열의 원소) 대시만 있는 줄 아래에 적힌다.
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
        if (type.arrayOps != nullptr)
        {
            return ReadArray(document, node, type, value, error);
        }
        if (type.tableOps != nullptr)
        {
            return ReadTable(document, node, type, value, error);
        }
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
