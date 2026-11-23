#include <JBro/Asset/AssetMetaFile.h>

#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Reflection/ReflectedYaml.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        Bool Fail(AssetMetaError& error, std::size_t line, const char* message)
        {
            error.line = line;
            error.message = message;
            return false;
        }

        Bool ReadId(const YamlDocument& document, UInt32 node, const char* key, AssetId& result)
        {
            String text;
            if (false == document.FindScalar(node, key, text))
            {
                return false;
            }
            return Uuid::Parse(text.c_str(), text.size(), result);
        }

        // 내장 그림의 해시는 16 자리 16 진수다. 0 은 "없음" 이라 받지 않는다.
        Bool ParseHash(const String& text, UInt64& result)
        {
            if (text.size() != 16)
            {
                return false;
            }
            std::uint64_t value = 0;
            for (const char digit : text)
            {
                value <<= 4;
                if (digit >= '0' && digit <= '9')
                {
                    value |= static_cast<std::uint64_t>(digit - '0');
                }
                else if (digit >= 'a' && digit <= 'f')
                {
                    value |= static_cast<std::uint64_t>(digit - 'a' + 10);
                }
                else if (digit >= 'A' && digit <= 'F')
                {
                    value |= static_cast<std::uint64_t>(digit - 'A' + 10);
                }
                else
                {
                    return false;
                }
            }
            result = value;
            return value != 0;
        }

        void FormatHash(UInt64 hash, char (&text)[17])
        {
            static constexpr char Digits[] = "0123456789abcdef";
            std::uint64_t value = hash.Get();
            for (std::size_t index = 16; index > 0; --index)
            {
                text[index - 1] = Digits[value & 0xF];
                value >>= 4;
            }
            text[16] = '\0';
        }

        // `owner` 맵의 `ImportOptions` 를 표로 읽는다. 맵이나 블록이 없으면 `present` 가 거짓이고 참이다.
        Bool ReadOptions(const YamlDocument& document, UInt32 owner, const TypeDescriptor& descriptor,
            void* options, Bool& present, AssetMetaError& error)
        {
            present = false;
            if (owner == YamlDocument::InvalidNode || document.GetKind(owner) != YamlKind::Map)
            {
                return true;
            }
            const UInt32 block = document.Find(owner, "ImportOptions");
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
                return Fail(error, document.GetLine(block), message.c_str());
            }
            present = true;
            return true;
        }

        Bool Interpret(const YamlDocument& document, AssetMetaFile& result, AssetMetaError& error)
        {
            const UInt32 root = document.GetRoot();
            if (root == YamlDocument::InvalidNode || document.GetKind(root) != YamlKind::Map)
            {
                return Fail(error, 1, "an asset meta file is a map");
            }

            AssetMetaFile parsed;
            Int64 version = 1;
            if (document.Find(root, "Version") != YamlDocument::InvalidNode
                && false == document.FindInt(root, "Version", version))
            {
                return Fail(error, document.GetLine(document.Find(root, "Version")), "Version must be a whole number");
            }
            if (version != 1)
            {
                return Fail(error, document.GetLine(document.Find(root, "Version")), "this meta file version is not one this engine reads");
            }
            parsed.version = static_cast<JBro::UInt32>(version);

            if (false == ReadId(document, root, "Id", parsed.id) || parsed.id.IsNull())
            {
                return Fail(error, document.GetLine(document.Find(root, "Id")), "Id must be 32 hex digits and not null");
            }

            String typeName;
            if (false == document.FindScalar(root, "Type", typeName))
            {
                return Fail(error, 1, "Type is required");
            }
            parsed.type = AssetTypeRules::ParseTypeName(typeName);
            if (parsed.type == AssetType::Unknown)
            {
                return Fail(error, document.GetLine(document.Find(root, "Type")), "Type is not a name this engine knows");
            }

            const UInt32 sprite = document.Find(root, "Sprite");
            if (AssetTypeRules::IsImageType(parsed.type))
            {
                if (sprite == YamlDocument::InvalidNode || document.GetKind(sprite) != YamlKind::Map
                    || false == ReadId(document, sprite, "Id", parsed.spriteId) || parsed.spriteId.IsNull())
                {
                    return Fail(error, sprite != YamlDocument::InvalidNode ? document.GetLine(sprite) : document.GetLine(root), "an image needs a Sprite block with its own Id");
                }
                if (parsed.spriteId == parsed.id)
                {
                    return Fail(error, document.GetLine(sprite), "the sprite id must differ from the texture id");
                }
            }

            // 임포트 옵션 블록. 있으면 전부 읽혀야 한다.
            const UInt32 texture = document.Find(root, "Texture");
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
            const UInt32 audio = document.Find(root, "Audio");
            if (false == ReadOptions(document, audio, TypeDescriptorOf<AudioImportOptions>::Get(),
                    &parsed.audioOptions, parsed.hasAudioOptions, error))
            {
                return false;
            }
            const UInt32 font = document.Find(root, "Font");
            if (false == ReadOptions(document, font, TypeDescriptorOf<FontImportOptions>::Get(),
                    &parsed.fontOptions, parsed.hasFontOptions, error))
            {
                return false;
            }
            const UInt32 family = document.Find(root, "FontFamily");
            if (false == ReadOptions(document, family, TypeDescriptorOf<FontFamilyOptions>::Get(),
                    &parsed.fontFamilyOptions, parsed.hasFontFamilyOptions, error))
            {
                return false;
            }
            const UInt32 strings = document.Find(root, "StringTable");
            if (false == ReadOptions(document, strings, TypeDescriptorOf<StringTableOptions>::Get(),
                    &parsed.stringTableOptions, parsed.hasStringTableOptions, error))
            {
                return false;
            }
            // 모델 블록: 옵션과 하위 에셋 목록(D-391). 목록의 항목은 `Key` 와 `Id` 이고 키는 겹치지 않으며 아이디는 모델·서로와 겹치지 않는다.
            const UInt32 model = document.Find(root, "Model");
            if (false == ReadOptions(document, model, TypeDescriptorOf<ModelImportOptions>::Get(),
                    &parsed.modelOptions, parsed.hasModelOptions, error))
            {
                return false;
            }
            const UInt32 subAssets = model != YamlDocument::InvalidNode && document.GetKind(model) == YamlKind::Map
                ? document.Find(model, "SubAssets") : YamlDocument::InvalidNode;
            if (subAssets != YamlDocument::InvalidNode)
            {
                if (document.GetKind(subAssets) != YamlKind::Sequence)
                {
                    return Fail(error, document.GetLine(subAssets), "Model.SubAssets must be a list");
                }
                parsed.hasSubAssets = true;
                for (std::size_t at = 0; at < document.GetCount(subAssets); ++at)
                {
                    const UInt32 item = document.GetElement(subAssets, at);
                    AssetSubEntry entry;
                    AssetType subType = AssetType::Unknown;
                    std::string_view subName;
                    if (document.GetKind(item) != YamlKind::Map || false == document.FindScalar(item, "Key", entry.key)
                        || false == SplitSubAssetKey(entry.key, subType, subName))
                    {
                        return Fail(error, document.GetLine(item), "a sub-asset needs a Key like Mesh/Name");
                    }
                    if (false == ReadId(document, item, "Id", entry.id) || entry.id.IsNull() || entry.id == parsed.id)
                    {
                        return Fail(error, document.GetLine(item), "a sub-asset needs its own Id");
                    }
                    // 내장 그림의 해시와 옵션. 다른 종류에 있으면 실패다 - 읽고 버리면 고친 값이 조용히 사라진다.
                    const UInt32 hashNode = document.Find(item, "Hash");
                    if (hashNode != YamlDocument::InvalidNode)
                    {
                        String hashText;
                        if (subType != AssetType::Texture || false == document.FindScalar(item, "Hash", hashText)
                            || false == ParseHash(hashText, entry.contentHash))
                        {
                            return Fail(error, document.GetLine(hashNode), "only a Texture sub-asset has a Hash of 16 hex digits");
                        }
                    }
                    if (subType != AssetType::Texture && document.Find(item, "ImportOptions") != YamlDocument::InvalidNode)
                    {
                        return Fail(error, document.GetLine(item), "only a Texture sub-asset has ImportOptions");
                    }
                    if (false == ReadOptions(document, item, TypeDescriptorOf<TextureImportOptions>::Get(),
                            &entry.textureOptions, entry.hasTextureOptions, error))
                    {
                        return false;
                    }
                    for (std::size_t seen = 0; seen < parsed.subAssets.Size(); ++seen)
                    {
                        if (parsed.subAssets[seen].key == entry.key || parsed.subAssets[seen].id == entry.id)
                        {
                            return Fail(error, document.GetLine(item), "two sub-assets share a key or an id");
                        }
                    }
                    parsed.subAssets.Add(std::move(entry));
                }
            }

            result = parsed;
            return true;
        }
    }

    Bool SplitSubAssetKey(std::string_view key, AssetType& type, std::string_view& name)
    {
        const std::size_t slash = key.find('/');
        if (slash == std::string_view::npos || slash == 0 || slash + 1 >= key.size())
        {
            return false;
        }
        type = AssetTypeRules::ParseTypeName(key.substr(0, slash));
        name = key.substr(slash + 1);
        // 모델의 하위 에셋은 메시·재질·내장 그림과 뼈대·애니메이션(D-456)이다.
        return type == AssetType::Mesh || type == AssetType::Material || type == AssetType::Texture || type == AssetType::Skeleton
            || type == AssetType::AnimationClip;
    }

    Bool ParseAssetMetaFile(const char* text, std::size_t length, AssetMetaFile& result, AssetMetaError& error)
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
    Bool LoadAssetMetaFile(IPlatform& platform, const char* utf8Path, AssetMetaFile& result, AssetMetaError& error)
    {
        Array<std::byte> contents;
        if (false == platform.ReadWholeFile(utf8Path, contents))
        {
            return Fail(error, 0, "the meta file could not be read");
        }
        return ParseAssetMetaFile(reinterpret_cast<const char*>(contents.Data()), contents.Size(), result, error);
    }

    Bool FormatAssetMetaFile(const AssetMetaFile& meta, String& text)
    {
        const Bool image = AssetTypeRules::IsImageType(meta.type);
        if (meta.id.IsNull() || meta.type == AssetType::Unknown || (image && meta.spriteId.IsNull()))
        {
            // 읽는 쪽이 거절할 것을 적지 않는다.
            return false;
        }
        char idText[Uuid::TextCapacity];
        YamlWriter writer;
        writer.WriteInt("Version", static_cast<JBro::Int64>(meta.version));
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
        // 오디오가 아닌 타입의 오디오 옵션은 뜻이 없어 적지 않는다.
        if (meta.hasAudioOptions && meta.type == AssetType::Audio)
        {
            writer.BeginMap("Audio");
            if (false == WriteReflectedValue(writer, "ImportOptions", TypeDescriptorOf<AudioImportOptions>::Get(),
                    &meta.audioOptions, error))
            {
                return false;
            }
            writer.EndMap();
        }
        if (meta.hasFontOptions && meta.type == AssetType::Font)
        {
            writer.BeginMap("Font");
            if (false == WriteReflectedValue(writer, "ImportOptions", TypeDescriptorOf<FontImportOptions>::Get(),
                    &meta.fontOptions, error))
            {
                return false;
            }
            writer.EndMap();
        }
        if (meta.hasStringTableOptions && meta.type == AssetType::StringTable)
        {
            writer.BeginMap("StringTable");
            if (false == WriteReflectedValue(writer, "ImportOptions", TypeDescriptorOf<StringTableOptions>::Get(),
                    &meta.stringTableOptions, error))
            {
                return false;
            }
            writer.EndMap();
        }
        // 모델 블록은 모델에만 적는다. 옵션과 하위 목록 둘 다 없으면 블록이 없다 - 다음 에디터 스캔이 모델을 읽어 채운다.
        if (meta.type == AssetType::Model && (meta.hasModelOptions || meta.hasSubAssets))
        {
            writer.BeginMap("Model");
            if (meta.hasModelOptions && false == WriteReflectedValue(writer, "ImportOptions", TypeDescriptorOf<ModelImportOptions>::Get(),
                    &meta.modelOptions, error))
            {
                return false;
            }
            if (meta.hasSubAssets)
            {
                writer.BeginSequence("SubAssets");
                for (std::size_t index = 0; index < meta.subAssets.Size(); ++index)
                {
                    const AssetSubEntry& entry = meta.subAssets[index];
                    AssetType subType = AssetType::Unknown;
                    std::string_view subName;
                    if (entry.id.IsNull() || false == SplitSubAssetKey(entry.key, subType, subName))
                    {
                        return false;
                    }
                    writer.BeginMap(nullptr);
                    writer.WriteString("Key", entry.key.c_str());
                    entry.id.ToText(idText, sizeof(idText));
                    writer.WriteString("Id", idText);
                    // 읽는 쪽이 거절할 것을 적지 않는다 - 메시·재질의 해시·옵션은 파일을 쓰지 않는다.
                    if (subType != AssetType::Texture && (entry.contentHash != 0 || entry.hasTextureOptions))
                    {
                        return false;
                    }
                    if (entry.contentHash != 0)
                    {
                        char hashText[17];
                        FormatHash(entry.contentHash, hashText);
                        writer.WriteString("Hash", hashText);
                    }
                    if (entry.hasTextureOptions && false == WriteReflectedValue(writer, "ImportOptions",
                            TypeDescriptorOf<TextureImportOptions>::Get(), &entry.textureOptions, error))
                    {
                        return false;
                    }
                    writer.EndMap();
                }
                writer.EndSequence();
            }
            writer.EndMap();
        }
        if (meta.hasFontFamilyOptions && meta.type == AssetType::FontFamily)
        {
            writer.BeginMap("FontFamily");
            if (false == WriteReflectedValue(writer, "ImportOptions", TypeDescriptorOf<FontFamilyOptions>::Get(),
                    &meta.fontFamilyOptions, error))
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

    Bool SaveAssetMetaFile(IPlatform& platform, const char* utf8Path, const AssetMetaFile& meta)
    {
        String text;
        if (false == FormatAssetMetaFile(meta, text))
        {
            return false;
        }
        JArrayView<std::byte> view;
        view.data = reinterpret_cast<const std::byte*>(text.data());
        view.size = static_cast<JBro::UInt32>(text.size());
        // 임시 파일에 다 쓴 뒤 바꿔치기한다. 메타를 쓰다 말면 아이디를 잃고, 그것은 다시 들여와도 돌아오지 않는다.
        // 옮기기가 없는 플랫폼은 그대로 덮어쓴다.
        const String scratch = AssetTypeRules::MakeMetaScratchPath(utf8Path);
        if (false == platform.WriteWholeFile(scratch.c_str(), view))
        {
            return false;
        }
        if (platform.MoveFileTo(scratch.c_str(), utf8Path))
        {
            return true;
        }
        return platform.WriteWholeFile(utf8Path, view);
    }
}
