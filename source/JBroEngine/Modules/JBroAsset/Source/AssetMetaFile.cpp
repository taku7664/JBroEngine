#include <JBro/Asset/AssetMetaFile.h>

#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Reflection/ReflectedYaml.h>
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

        // `owner` 맵의 `ImportOptions` 를 표로 읽는다. 맵이나 블록이 없으면 `present` 가 거짓이고 참이다.
        bool ReadOptions(const YamlDocument& document, std::uint32_t owner, const TypeDescriptor& descriptor,
            void* options, bool& present, AssetMetaError& error)
        {
            present = false;
            if (owner == YamlDocument::InvalidNode || document.GetKind(owner) != YamlKind::Map)
            {
                return true;
            }
            const std::uint32_t block = document.Find(owner, "ImportOptions");
            if (block == YamlDocument::InvalidNode)
            {
                return true;
            }
            ReflectedYamlError reflected;
            if (false == ReadReflectedValue(document, block, descriptor, options, reflected))
            {
                String message = "ImportOptions could not be read";
                if (false == reflected.fieldName.empty())
                {
                    message.append(" at ");
                    message.append(reflected.fieldName);
                }
                if (false == reflected.message.empty())
                {
                    message.append(": ");
                    message.append(reflected.message);
                }
                return Fail(error, 1, message.c_str());
            }
            present = true;
            return true;
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
            if (version != 1)
            {
                return Fail(error, 1, "this meta file version is not one this engine reads");
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

            // 임포트 옵션 블록. 있으면 전부 읽혀야 한다.
            const std::uint32_t texture = document.Find(root, "Texture");
            if (false == ReadOptions(document, texture, TypeDescriptorOf<TextureImportOptions>::Get(),
                    &parsed.textureOptions, parsed.hasTextureOptions, error))
            {
                return false;
            }
            if (false == ReadOptions(document, sprite, TypeDescriptorOf<SpriteImportOptions>::Get(),
                    &parsed.spriteOptions, parsed.hasSpriteOptions, error))
            {
                return false;
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

    bool FormatAssetMetaFile(const AssetMetaFile& meta, String& text)
    {
        const bool image = AssetTypeRules::IsImageType(meta.type);
        if (meta.id.IsNull() || meta.type == AssetType::Unknown || (image && meta.spriteId.IsNull()))
        {
            // 읽는 쪽이 거절할 것을 적지 않는다.
            return false;
        }
        char idText[Uuid::TextCapacity];
        YamlWriter writer;
        writer.WriteInt("Version", static_cast<std::int64_t>(meta.version));
        meta.id.ToText(idText, sizeof(idText));
        writer.WriteString("Id", idText);
        writer.WriteString("Type", AssetTypeRules::GetTypeName(meta.type));
        ReflectedYamlError error;
        if (meta.hasTextureOptions)
        {
            writer.BeginMap("Texture");
            if (false == WriteReflectedValue(writer, "ImportOptions", TypeDescriptorOf<TextureImportOptions>::Get(),
                    &meta.textureOptions, error))
            {
                return false;
            }
            writer.EndMap();
        }
        if (image)
        {
            writer.BeginMap("Sprite");
            meta.spriteId.ToText(idText, sizeof(idText));
            writer.WriteString("Id", idText);
            // 이미지가 아닌 타입의 스프라이트 옵션은 뜻이 없어 적지 않는다.
            if (meta.hasSpriteOptions
                && false == WriteReflectedValue(writer, "ImportOptions", TypeDescriptorOf<SpriteImportOptions>::Get(),
                    &meta.spriteOptions, error))
            {
                return false;
            }
            writer.EndMap();
        }
        text = writer.GetText();
        return true;
    }

    String FormatAssetMetaFile(const AssetMetaFile& meta)
    {
        String text;
        if (false == FormatAssetMetaFile(meta, text))
        {
            return String();
        }
        return text;
    }

    bool SaveAssetMetaFile(IPlatform& platform, const char* utf8Path, const AssetMetaFile& meta)
    {
        String text;
        if (false == FormatAssetMetaFile(meta, text))
        {
            return false;
        }
        JArrayView<std::byte> view;
        view.data = reinterpret_cast<const std::byte*>(text.data());
        view.size = static_cast<std::uint32_t>(text.size());
        return platform.WriteWholeFile(utf8Path, view);
    }
}
