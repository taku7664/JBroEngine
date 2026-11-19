#include <JBro/Graphics/PostProcessProfile.h>

#include <JBro/Core/Yaml.h>
#include <JBro/Reflection/ReflectedYaml.h>
#include <JBro/Types/NameTable.h>

#include <cstdio>
#include <cstring>
#include <string_view>

namespace JBro
{
    namespace
    {
        const PropertyTable* ParametersOf(UInt32 type)
        {
            const PostProcessEffectType* effect = PostProcessEffectRegistry::Get(type);
            return effect != nullptr ? effect->parameters : nullptr;
        }

        std::string_view NameOf(const PropertyInfo& property)
        {
            return NameTable::Get().Resolve(property.name);
        }

        void Warn(Array<String>* warnings, const String& typeName, std::string_view what, std::string_view field)
        {
            if (warnings == nullptr)
            {
                return;
            }
            String line(typeName);
            line.append(".");
            line.append(field.data(), field.size());
            line.append(": ");
            line.append(what.data(), what.size());
            warnings->Add(std::move(line));
        }
    }

    PostProcessProfileEntry MakePostProcessProfileEntry(UInt32 type)
    {
        PostProcessProfileEntry entry;
        const PostProcessEffectType* effect = PostProcessEffectRegistry::Get(type);
        if (effect == nullptr)
        {
            return entry;
        }
        entry.typeName = effect->name;
        entry.type = type;
        entry.block.Resize(effect->blockSize);
        if (effect->blockSize != 0)
        {
            std::memcpy(entry.block.Data(), effect->defaults, effect->blockSize.Get());
        }
        return entry;
    }

    Bool IsPostProcessFieldEditable(UInt32 type, UInt32 field)
    {
        const PropertyTable* table = ParametersOf(type);
        return table != nullptr && field < table->count && field < 64 && table->properties[field.Get()].serialize
            && table->properties[field.Get()].type != nullptr;
    }

    Bool SetPostProcessOverride(PostProcessProfileEntry& entry, UInt32 field, Bool overridden)
    {
        const PostProcessEffectType* effect = PostProcessEffectRegistry::Get(entry.type);
        if (effect == nullptr || false == IsPostProcessFieldEditable(entry.type, field) || entry.block.Size() != effect->blockSize)
        {
            return false;
        }
        const UInt64 bit = UInt64(1) << field.Get();
        if (overridden)
        {
            entry.overrides = entry.overrides | bit;
            return true;
        }
        entry.overrides = entry.overrides & ~bit;
        // 덮어쓰지 않는 필드는 기본값이다.
        const PropertyInfo& property = effect->parameters->properties[field.Get()];
        std::memcpy(property.Address(entry.block.Data()), property.ConstAddress(effect->defaults), property.type->size.Get());
        return true;
    }

    Bool ParsePostProcessProfile(const char* text, std::size_t length, PostProcessProfileDocument& document, String* error,
        Array<String>* warnings)
    {
        document.entries.Clear();
        if (text == nullptr || length == 0)
        {
            return true;
        }
        YamlDocument yaml;
        YamlError parseError;
        if (false == yaml.Parse(text, length, parseError))
        {
            if (error != nullptr)
            {
                char line[32] = {};
                std::snprintf(line, sizeof(line), "line %zu: ", parseError.line);
                *error = line;
                error->append(parseError.message.c_str(), parseError.message.size());
            }
            return false;
        }
        const UInt32 root = yaml.GetRoot();
        if (root == YamlDocument::InvalidNode || yaml.GetKind(root) != YamlKind::Map)
        {
            return true;
        }
        const UInt32 effects = yaml.Find(root, "Effects");
        if (effects == YamlDocument::InvalidNode || yaml.GetKind(effects) != YamlKind::Sequence)
        {
            return true;
        }
        for (std::size_t index = 0; index < yaml.GetCount(effects); ++index)
        {
            const UInt32 item = yaml.GetElement(effects, index);
            if (yaml.GetKind(item) != YamlKind::Map)
            {
                continue;
            }
            String typeName;
            if (false == yaml.FindScalar(item, "Type", typeName) || typeName.empty())
            {
                if (warnings != nullptr)
                {
                    warnings->Add(String("an effect without a Type is skipped"));
                }
                continue;
            }
            const UInt32 type = PostProcessEffectRegistry::Find(typeName.c_str());
            PostProcessProfileEntry entry = MakePostProcessProfileEntry(type);
            entry.typeName = typeName;
            Bool enabled = true;
            yaml.FindBool(item, "Enabled", enabled);
            entry.enabled = enabled;
            const UInt32 parameters = yaml.Find(item, "Parameters");
            if (parameters != YamlDocument::InvalidNode && yaml.GetKind(parameters) == YamlKind::Map)
            {
                const PropertyTable* table = ParametersOf(type);
                for (std::size_t key = 0; key < yaml.GetCount(parameters); ++key)
                {
                    const char* name = yaml.GetKey(parameters, key);
                    const UInt32 value = yaml.GetValue(parameters, key);
                    if (table == nullptr)
                    {
                        // 모르는 종류다. 스칼라만 그대로 되쓴다.
                        if (yaml.GetKind(value) == YamlKind::Scalar)
                        {
                            entry.unknownKeys.Add(String(name));
                            entry.unknownValues.Add(String(yaml.GetText(value)));
                        }
                        continue;
                    }
                    Bool found = false;
                    for (UInt32 field = 0; field < table->count; ++field)
                    {
                        const PropertyInfo& property = table->properties[field.Get()];
                        if (NameOf(property) != name)
                        {
                            continue;
                        }
                        found = true;
                        if (false == IsPostProcessFieldEditable(type, field))
                        {
                            Warn(warnings, typeName, "cannot be set in a profile", name);
                            break;
                        }
                        ReflectedYamlError readError;
                        if (ReadReflectedValue(yaml, value, *property.type, property.Address(entry.block.Data()), readError))
                        {
                            entry.overrides = entry.overrides | (UInt64(1) << field.Get());
                        }
                        else
                        {
                            // 반쯤 읽힌 값을 남기지 않는다.
                            const PostProcessEffectType* effect = PostProcessEffectRegistry::Get(type);
                            std::memcpy(property.Address(entry.block.Data()), property.ConstAddress(effect->defaults), property.type->size.Get());
                            Warn(warnings, typeName, readError.message, name);
                        }
                        break;
                    }
                    if (false == found)
                    {
                        Warn(warnings, typeName, "is not a field of this effect", name);
                    }
                }
            }
            document.entries.Add(std::move(entry));
        }
        return true;
    }

    String WritePostProcessProfile(const PostProcessProfileDocument& document)
    {
        YamlWriter writer;
        writer.BeginSequence("Effects");
        for (const PostProcessProfileEntry& entry : document.entries)
        {
            writer.BeginMap(nullptr);
            writer.WriteString("Type", entry.typeName.c_str());
            writer.WriteBool("Enabled", entry.enabled);
            const PropertyTable* table = ParametersOf(entry.type);
            const PostProcessEffectType* effect = PostProcessEffectRegistry::Get(entry.type);
            if (table != nullptr && effect != nullptr && entry.block.Size() == effect->blockSize)
            {
                if (entry.overrides != 0)
                {
                    writer.BeginMap("Parameters");
                    for (UInt32 field = 0; field < table->count && field < 64; ++field)
                    {
                        if ((entry.overrides & (UInt64(1) << field.Get())) == 0 || false == IsPostProcessFieldEditable(entry.type, field))
                        {
                            continue;
                        }
                        const PropertyInfo& property = table->properties[field.Get()];
                        const std::string_view name = NameOf(property);
                        const String key(name.data(), name.size());
                        ReflectedYamlError writeError;
                        WriteReflectedValue(writer, key.c_str(), *property.type, property.ConstAddress(entry.block.Data()), writeError);
                    }
                    writer.EndMap();
                }
            }
            else if (false == entry.unknownKeys.IsEmpty())
            {
                writer.BeginMap("Parameters");
                for (std::size_t at = 0; at < entry.unknownKeys.Size() && at < entry.unknownValues.Size(); ++at)
                {
                    writer.WriteString(entry.unknownKeys[at].c_str(), entry.unknownValues[at].c_str());
                }
                writer.EndMap();
            }
            writer.EndMap();
        }
        writer.EndSequence();
        return writer.GetText();
    }
}
