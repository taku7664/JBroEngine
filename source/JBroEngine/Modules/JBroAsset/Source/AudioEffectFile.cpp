#include <JBro/Asset/AudioEffectFile.h>

#include <JBro/Core/Yaml.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        Float ClampToParam(Float value, const AudioEffectParam& param)
        {
            if (false == std::isfinite(value))
            {
                return param.defaultValue;
            }
            return value < param.minValue ? param.minValue : (value > param.maxValue ? param.maxValue : value);
        }
    }

    Bool ParseAudioEffect(const char* text, std::size_t length, AudioEffectSettings& settings, String* error)
    {
        settings = MakeAudioEffect(AudioEffectKind::Reverb);
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
        UInt32 effect = document.GetRoot();
        if (effect == YamlDocument::InvalidNode || document.GetKind(effect) != YamlKind::Map)
        {
            return true;
        }
        const UInt32 wrapped = document.Find(effect, "Effect");
        if (wrapped != YamlDocument::InvalidNode && document.GetKind(wrapped) == YamlKind::Map)
        {
            effect = wrapped;
        }
        String kindName;
        if (document.FindScalar(effect, "Kind", kindName))
        {
            for (UInt32 index = 0; index < static_cast<JBro::UInt32>(AudioEffectKind::Count); ++index)
            {
                if (std::strcmp(kindName.c_str(), AudioEffectKinds[index].name) == 0)
                {
                    settings = MakeAudioEffect(static_cast<AudioEffectKind>(index.Get()));
                    break;
                }
            }
        }
        const AudioEffectKindInfo& info = GetAudioEffectKindInfo(settings.kind);
        const UInt32 parameters = document.Find(effect, "Parameters");
        if (parameters == YamlDocument::InvalidNode || document.GetKind(parameters) != YamlKind::Map)
        {
            return true;
        }
        for (UInt32 index = 0; index < info.paramCount; ++index)
        {
            Float value = info.params[index].defaultValue;
            if (document.FindFloat(parameters, info.params[index].name, value))
            {
                settings.values[index] = ClampToParam(value, info.params[index]);
            }
        }
        return true;
    }

    String WriteAudioEffect(const AudioEffectSettings& settings)
    {
        const AudioEffectKindInfo& info = GetAudioEffectKindInfo(settings.kind);
        YamlWriter writer;
        writer.BeginMap("Effect");
        writer.WriteString("Kind", info.name);
        writer.BeginMap("Parameters");
        for (UInt32 index = 0; index < info.paramCount; ++index)
        {
            writer.WriteFloat(info.params[index].name, ClampToParam(settings.values[index], info.params[index]));
        }
        writer.EndMap();
        writer.EndMap();
        return writer.GetText();
    }
}
