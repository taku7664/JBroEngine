#include <JBro/Asset/MaterialFile.h>

#include <JBro/Core/Yaml.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace JBro
{
    namespace
    {
        Bool ParseNumber(const char* text, Float& value)
        {
            if (text == nullptr || *text == '\0')
            {
                return false;
            }
            char* end = nullptr;
            const float read = std::strtof(text, &end);
            if (end == text || *end != '\0')
            {
                return false;
            }
            value = read;
            return true;
        }

        Bool ParseId(const String& text, AssetId& id)
        {
            Uuid parsed;
            if (false == Uuid::Parse(text.c_str(), text.size(), parsed))
            {
                return false;
            }
            id = AssetId(parsed);
            return true;
        }

        String IdText(const AssetId& id)
        {
            char text[Uuid::TextCapacity] = {};
            id.ToText(text, sizeof(text));
            return String(text);
        }

        void Warn(Array<String>* warnings, const char* name, const char* what)
        {
            if (warnings == nullptr)
            {
                return;
            }
            String line(name);
            line.append(": ");
            line.append(what);
            warnings->Add(std::move(line));
        }
    }

    MaterialParameterValue* MaterialDocument::FindParameter(std::string_view name) noexcept
    {
        for (MaterialParameterValue& parameter : parameters)
        {
            if (parameter.name.View() == name)
            {
                return &parameter;
            }
        }
        return nullptr;
    }

    const MaterialParameterValue* MaterialDocument::FindParameter(std::string_view name) const noexcept
    {
        for (const MaterialParameterValue& parameter : parameters)
        {
            if (parameter.name.View() == name)
            {
                return &parameter;
            }
        }
        return nullptr;
    }

    const MaterialTextureValue* MaterialDocument::FindTexture(std::string_view name) const noexcept
    {
        for (const MaterialTextureValue& texture : textures)
        {
            if (texture.name.View() == name)
            {
                return &texture;
            }
        }
        return nullptr;
    }

    void MaterialDocument::SetParameter(std::string_view name, const Float* values, UInt32 count)
    {
        MaterialParameterValue* parameter = FindParameter(name);
        if (parameter == nullptr)
        {
            MaterialParameterValue added;
            added.name.assign(name.data(), name.size());
            parameter = &parameters.Add(std::move(added));
        }
        parameter->count = count > 4 ? UInt32(4) : count;
        for (UInt32 at = 0; at < 4; ++at)
        {
            parameter->values[at.Get()] = at < parameter->count ? values[at.Get()] : Float(0.0f);
        }
    }

    void MaterialDocument::RemoveParameter(std::string_view name)
    {
        for (std::size_t at = 0; at < parameters.Size(); ++at)
        {
            if (parameters[at].name.View() == name)
            {
                parameters.RemoveAt(at);
                return;
            }
        }
    }

    void MaterialDocument::SetTexture(std::string_view name, AssetId texture)
    {
        for (std::size_t at = 0; at < textures.Size(); ++at)
        {
            if (textures[at].name.View() == name)
            {
                if (texture.IsNull())
                {
                    textures.RemoveAt(at);
                }
                else
                {
                    textures[at].texture = texture;
                }
                return;
            }
        }
        if (false == texture.IsNull())
        {
            MaterialTextureValue added;
            added.name.assign(name.data(), name.size());
            added.texture = texture;
            textures.Add(std::move(added));
        }
    }

    Bool ParseMaterial(const char* text, std::size_t length, MaterialDocument& document, String* error, Array<String>* warnings)
    {
        document = {};
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
        String shader;
        if (yaml.FindScalar(root, "Shader", shader) && false == shader.empty() && false == ParseId(shader, document.shader))
        {
            Warn(warnings, "Shader", "is not an asset id");
        }
        String domain;
        if (yaml.FindScalar(root, "Domain", domain) && false == domain.empty())
        {
            if (domain == "PostProcess")
            {
                document.domain = MaterialDomain::PostProcess;
            }
            else if (domain == "Sprite")
            {
                document.domain = MaterialDomain::Sprite;
            }
            else
            {
                if (error != nullptr)
                {
                    *error = "Domain: unknown domain ";
                    error->append(domain.c_str(), domain.size());
                }
                return false;
            }
        }
        const UInt32 parameters = yaml.Find(root, "Parameters");
        if (parameters != YamlDocument::InvalidNode && yaml.GetKind(parameters) == YamlKind::Map)
        {
            for (std::size_t key = 0; key < yaml.GetCount(parameters); ++key)
            {
                const char* name = yaml.GetKey(parameters, key);
                const UInt32 value = yaml.GetValue(parameters, key);
                MaterialParameterValue parameter;
                parameter.name = name;
                Bool good = true;
                if (yaml.GetKind(value) == YamlKind::Scalar)
                {
                    good = ParseNumber(yaml.GetText(value), parameter.values[0]);
                    parameter.count = 1;
                }
                else if (yaml.GetKind(value) == YamlKind::Sequence && yaml.GetCount(value) >= 1 && yaml.GetCount(value) <= 4)
                {
                    parameter.count = static_cast<JBro::UInt32>(yaml.GetCount(value));
                    for (std::size_t at = 0; at < yaml.GetCount(value) && good; ++at)
                    {
                        const UInt32 element = yaml.GetElement(value, at);
                        good = yaml.GetKind(element) == YamlKind::Scalar && ParseNumber(yaml.GetText(element), parameter.values[at]);
                    }
                }
                else
                {
                    good = false;
                }
                if (false == good)
                {
                    Warn(warnings, name, "is not a number or a sequence of up to four numbers");
                    continue;
                }
                document.parameters.Add(std::move(parameter));
            }
        }
        const UInt32 textures = yaml.Find(root, "Textures");
        if (textures != YamlDocument::InvalidNode && yaml.GetKind(textures) == YamlKind::Map)
        {
            for (std::size_t key = 0; key < yaml.GetCount(textures); ++key)
            {
                const char* name = yaml.GetKey(textures, key);
                const UInt32 value = yaml.GetValue(textures, key);
                MaterialTextureValue texture;
                texture.name = name;
                if (yaml.GetKind(value) != YamlKind::Scalar || false == ParseId(String(yaml.GetText(value)), texture.texture))
                {
                    Warn(warnings, name, "is not an asset id");
                    continue;
                }
                document.textures.Add(std::move(texture));
            }
        }
        return true;
    }

    String WriteMaterial(const MaterialDocument& document)
    {
        YamlWriter writer;
        if (false == document.shader.IsNull())
        {
            writer.WriteString("Shader", IdText(document.shader).c_str());
        }
        writer.WriteString("Domain", document.domain == MaterialDomain::Sprite ? "Sprite" : "PostProcess");
        if (false == document.parameters.IsEmpty())
        {
            writer.BeginMap("Parameters");
            for (const MaterialParameterValue& parameter : document.parameters)
            {
                if (parameter.count <= 1)
                {
                    writer.WriteFloat(parameter.name.c_str(), parameter.values[0]);
                    continue;
                }
                writer.BeginSequence(parameter.name.c_str());
                for (UInt32 at = 0; at < parameter.count; ++at)
                {
                    writer.WriteFloatItem(parameter.values[at.Get()]);
                }
                writer.EndSequence();
            }
            writer.EndMap();
        }
        if (false == document.textures.IsEmpty())
        {
            writer.BeginMap("Textures");
            for (const MaterialTextureValue& texture : document.textures)
            {
                writer.WriteString(texture.name.c_str(), IdText(texture.texture).c_str());
            }
            writer.EndMap();
        }
        return writer.GetText();
    }
}
