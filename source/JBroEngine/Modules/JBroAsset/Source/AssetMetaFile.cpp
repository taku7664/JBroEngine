#include <JBro/Asset/AssetMetaFile.h>

#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Platform/Platform.h>

namespace JBro
{
    namespace
    {
        bool Fail(AssetMetaError& error, std::size_t line, const char* message)
        {
            error.line = line;
            error.message = message;
            return false;
        }

        bool ReadId(const YamlDocument& document, std::uint32_t node, const char* key, AssetId& result)
        {
            String text;
            if (false == document.FindScalar(node, key, text))
            {
                return false;
            }
            return Uuid::Parse(text.c_str(), text.size(), result);
        }

        bool Interpret(const YamlDocument& document, AssetMetaFile& result, AssetMetaError& error)
        {
            const std::uint32_t root = document.GetRoot();
            if (root == YamlDocument::InvalidNode || document.GetKind(root) != YamlKind::Map)
            {
                return Fail(error, 1, "an asset meta file is a map");
            }

            AssetMetaFile parsed;
            std::int64_t version = 1;
            if (document.Find(root, "Version") != YamlDocument::InvalidNode
                && false == document.FindInt(root, "Version", version))
            {
                return Fail(error, 1, "Version must be a whole number");
            }
            parsed.version = static_cast<std::uint32_t>(version);

            if (false == ReadId(document, root, "Id", parsed.id) || parsed.id.IsNull())
            {
                return Fail(error, 1, "Id must be 32 hex digits and not null");
            }

            String typeName;
            if (false == document.FindScalar(root, "Type", typeName))
            {
                return Fail(error, 1, "Type is required");
            }
            parsed.type = AssetTypeRules::ParseTypeName(typeName);
            if (parsed.type == AssetType::Unknown)
            {
                return Fail(error, 1, "Type is not a name this engine knows");
            }

            const std::uint32_t sprite = document.Find(root, "Sprite");
            if (AssetTypeRules::IsImageType(parsed.type))
            {
                if (sprite == YamlDocument::InvalidNode || document.GetKind(sprite) != YamlKind::Map
                    || false == ReadId(document, sprite, "Id", parsed.spriteId) || parsed.spriteId.IsNull())
                {
                    return Fail(error, 1, "an image needs a Sprite block with its own Id");
                }
                if (parsed.spriteId == parsed.id)
                {
                    return Fail(error, 1, "the sprite id must differ from the texture id");
                }
            }

            result = parsed;
            return true;
        }
    }

    bool ParseAssetMetaFile(const char* text, std::size_t length, AssetMetaFile& result, AssetMetaError& error)
    {
        YamlDocument document;
        YamlError yamlError;
        if (false == document.Parse(text, length, yamlError))
        {
            return Fail(error, yamlError.line, yamlError.message.c_str());
        }
        return Interpret(document, result, error);
    }

    // 파일은 플랫폼이 연다(D-112). 엔진 모듈은 파일을 직접 열지 않는다.
    bool LoadAssetMetaFile(IPlatform& platform, const char* utf8Path, AssetMetaFile& result, AssetMetaError& error)
    {
        Array<std::byte> contents;
        if (false == platform.ReadWholeFile(utf8Path, contents))
        {
            return Fail(error, 0, "the meta file could not be read");
        }
        return ParseAssetMetaFile(reinterpret_cast<const char*>(contents.Data()), contents.Size(), result, error);
    }

    String FormatAssetMetaFile(const AssetMetaFile& meta)
    {
        char idText[Uuid::TextCapacity];
        YamlWriter writer;
        writer.WriteInt("Version", static_cast<std::int64_t>(meta.version));
        meta.id.ToText(idText, sizeof(idText));
        writer.WriteString("Id", idText);
        writer.WriteString("Type", AssetTypeRules::GetTypeName(meta.type));
        if (AssetTypeRules::IsImageType(meta.type))
        {
            writer.BeginMap("Sprite");
            meta.spriteId.ToText(idText, sizeof(idText));
            writer.WriteString("Id", idText);
            writer.EndMap();
        }
        return writer.GetText();
    }

    bool SaveAssetMetaFile(IPlatform& platform, const char* utf8Path, const AssetMetaFile& meta)
    {
        const String text = FormatAssetMetaFile(meta);
        JArrayView<std::byte> view;
        view.data = reinterpret_cast<const std::byte*>(text.data());
        view.size = static_cast<std::uint32_t>(text.size());
        return platform.WriteWholeFile(utf8Path, view);
    }
}
