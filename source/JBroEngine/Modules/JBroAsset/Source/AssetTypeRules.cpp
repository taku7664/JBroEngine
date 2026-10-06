#include <JBro/Asset/AssetTypeRules.h>

#include <cstring>
#include <string_view>

namespace JBro
{
    namespace
    {
        struct TypeName
        {
            AssetType type;
            const char* name;
        };

        constexpr TypeName TypeNames[] =
        {
            {AssetType::Unknown, "Unknown"},
            {AssetType::Texture, "Texture"},
            {AssetType::Sprite, "Sprite"},
            {AssetType::Mesh, "Mesh"},
            {AssetType::Material, "Material"},
            {AssetType::Shader, "Shader"},
            {AssetType::Canvas, "Canvas"},
            {AssetType::Prefab, "Prefab"},
            {AssetType::Audio, "Audio"},
            {AssetType::Font, "Font"},
            {AssetType::FontFamily, "FontFamily"},
            {AssetType::StringTable, "StringTable"},
            {AssetType::Layer, "Layer"},
        };

        struct Extension
        {
            const char* extension;
            AssetType type;
        };

        // 기존 엔진의 표다(asset-plan §2.3). 스크립트 소스는 에셋이 아니라 컴파일러의 것이라 여기 없다.
        constexpr Extension Extensions[] =
        {
            {".png", AssetType::Texture},
            {".jpg", AssetType::Texture},
            {".jpeg", AssetType::Texture},
            {".bmp", AssetType::Texture},
            {".tga", AssetType::Texture},
            {".obj", AssetType::Mesh},
            {".gltf", AssetType::Mesh},
            {".glb", AssetType::Mesh},
            {".jmat", AssetType::Material},
            {".hlsl", AssetType::Shader},
            {".jcanvas", AssetType::Canvas},
            {".jprefab", AssetType::Prefab},
            {".wav", AssetType::Audio},
            {".ogg", AssetType::Audio},
            {".mp3", AssetType::Audio},
            {".flac", AssetType::Audio},
            {".ttf", AssetType::Font},
            {".otf", AssetType::Font},
            {".jfontfamily", AssetType::FontFamily},
            {".jstrings", AssetType::StringTable},
            {".jlayer", AssetType::Layer},
        };

        constexpr char MetaExtension[] = ".jmeta";

        bool EqualsIgnoringCase(std::string_view left, const char* right) noexcept
        {
            const std::size_t length = std::strlen(right);
            if (left.size() != length)
            {
                return false;
            }
            for (std::size_t index = 0; index < length; ++index)
            {
                char a = left[index];
                char b = right[index];
                if (a >= 'A' && a <= 'Z')
                {
                    a = static_cast<char>(a - 'A' + 'a');
                }
                if (b >= 'A' && b <= 'Z')
                {
                    b = static_cast<char>(b - 'A' + 'a');
                }
                if (a != b)
                {
                    return false;
                }
            }
            return true;
        }

        // 마지막 `.` 부터 끝까지다. 경로 구분자 뒤의 `.` 만 본다 - `folder.v2/name` 의 `.v2/name` 은 확장자가 아니다.
        std::string_view ExtensionOf(std::string_view path) noexcept
        {
            std::size_t index = path.size();
            while (index > 0)
            {
                const char c = path[index - 1];
                if (c == '/' || c == '\\')
                {
                    return {};
                }
                if (c == '.')
                {
                    return path.substr(index - 1);
                }
                --index;
            }
            return {};
        }
    }

    namespace AssetTypeRules
    {
        const char* GetTypeName(AssetType type) noexcept
        {
            for (const TypeName& entry : TypeNames)
            {
                if (entry.type == type)
                {
                    return entry.name;
                }
            }
            return "Unknown";
        }

        AssetType ParseTypeName(std::string_view name) noexcept
        {
            for (const TypeName& entry : TypeNames)
            {
                if (name == entry.name)
                {
                    return entry.type;
                }
            }
            return AssetType::Unknown;
        }

        AssetType DetectTypeFromPath(std::string_view path) noexcept
        {
            const std::string_view extension = ExtensionOf(path);
            if (extension.empty())
            {
                return AssetType::Unknown;
            }
            for (const Extension& entry : Extensions)
            {
                if (EqualsIgnoringCase(extension, entry.extension))
                {
                    return entry.type;
                }
            }
            return AssetType::Unknown;
        }

        AssetType TypeOfIdFieldName(std::string_view fieldName) noexcept
        {
            char buffer[32] = {};
            if (fieldName.size() <= 2 || fieldName.size() - 2 >= sizeof(buffer))
            {
                return AssetType::Unknown;
            }
            const std::size_t length = fieldName.size() - 2;
            for (std::size_t index = 0; index < length; ++index)
            {
                buffer[index] = fieldName[index];
            }
            // 소리는 흔히 "클립" 이라 부른다(`AudioSource::clipId`). 타입 이름과 다른 유일한 별명이다.
            if (std::string_view(buffer, length) == "clip")
            {
                return AssetType::Audio;
            }
            if (buffer[0] >= 'a' && buffer[0] <= 'z')
            {
                buffer[0] = static_cast<char>(buffer[0] - 'a' + 'A');
            }
            const AssetType whole = ParseTypeName(std::string_view(buffer, length));
            if (whole != AssetType::Unknown)
            {
                return whole;
            }
            // 이름의 마지막 낱말이 타입이면 그것이다(`regularFontId`·`boldItalicFontId` 는 Font).
            std::size_t last = length;
            while (last > 1 && false == (buffer[last - 1] >= 'A' && buffer[last - 1] <= 'Z'))
            {
                --last;
            }
            if (last <= 1)
            {
                return AssetType::Unknown;
            }
            return ParseTypeName(std::string_view(buffer + last - 1, length - last + 1));
        }

        bool IsImageType(AssetType type) noexcept
        {
            return type == AssetType::Texture;
        }

        const char* GetMetaExtension() noexcept
        {
            return MetaExtension;
        }

        bool IsMetaPath(std::string_view path) noexcept
        {
            return EqualsIgnoringCase(ExtensionOf(path), MetaExtension);
        }

        String MakeMetaScratchPath(std::string_view metaPath)
        {
            String result(metaPath);
            result.append(".tmp");
            return result;
        }

        bool IsMetaScratchPath(std::string_view path) noexcept
        {
            if (false == EqualsIgnoringCase(ExtensionOf(path), ".tmp"))
            {
                return false;
            }
            return IsMetaPath(path.substr(0, path.size() - 4));
        }

        String MakeMetaPath(std::string_view path)
        {
            String result(path);
            result.append(MetaExtension);
            return result;
        }
    }
}
