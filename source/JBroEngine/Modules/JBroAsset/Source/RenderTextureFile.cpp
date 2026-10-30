#include <JBro/Asset/RenderTextureFile.h>

#include <JBro/Core/Yaml.h>

#include <cstdio>
#include <cstring>
#include <JBro/Types/Int.h>

namespace JBro
{
    namespace
    {
        UInt32 ClampSide(Int64 value)
        {
            if (value < 1)
            {
                return 1;
            }
            if (value > static_cast<JBro::Int64>(RenderTextureSettings::MaxSide.Get()))
            {
                return RenderTextureSettings::MaxSide;
            }
            return UInt32(static_cast<std::uint32_t>(value.Get()));
        }
    }

    Bool ParseRenderTexture(const char* text, std::size_t length, RenderTextureSettings& settings, String* error)
    {
        settings = RenderTextureSettings{};
        if (text == nullptr || length == 0)
        {
            return true;
        }
        YamlDocument document;
        YamlError parseError;
        if (false == document.Parse(text, length, parseError))
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
        const UInt32 root = document.GetRoot();
        if (root == YamlDocument::InvalidNode || document.GetKind(root) != YamlKind::Map)
        {
            return true;
        }
        Int64 side = 0;
        if (document.FindInt(root, "Width", side))
        {
            settings.width = ClampSide(side);
        }
        if (document.FindInt(root, "Height", side))
        {
            settings.height = ClampSide(side);
        }
        String filter;
        if (document.FindScalar(root, "Filter", filter))
        {
            settings.filter = std::strcmp(filter.c_str(), "Nearest") == 0 ? TextureFilter::Nearest : TextureFilter::Linear;
        }
        return true;
    }

    String WriteRenderTexture(const RenderTextureSettings& settings)
    {
        YamlWriter writer;
        writer.WriteInt("Width", static_cast<JBro::Int64>(ClampSide(static_cast<JBro::Int64>(settings.width.Get())).Get()));
        writer.WriteInt("Height", static_cast<JBro::Int64>(ClampSide(static_cast<JBro::Int64>(settings.height.Get())).Get()));
        writer.WriteString("Filter", settings.filter == TextureFilter::Nearest ? "Nearest" : "Linear");
        return writer.GetText();
    }
}
