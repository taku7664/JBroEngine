#include <JBro/ScriptApiCatalog2D/Catalog.h>
#include "CatalogBuilder.h"
#include <cstring>
#include <filesystem>
#include <stdexcept>
// 엔진 빌드가 만든 원본 줄 -> 스크립트 SDK 사본 줄 대응표다(Modules/JBroScriptSdk).
#include <ScriptSdkLineMap2D.inc>

namespace JBro::ScriptApiCatalog2D::Internal
{
    // 정의는 사용자가 보는 스크립트 SDK 사본(`Build/ScriptSdk/2D/Include`)을 가리킨다. 엔진 소스의 헤더는 주석째로 사용자에게 가지 않는다.
    static String HeaderUri(const char* sdkRelative)
    {
        // /FC supplies a stable absolute source path; this is metadata, never a header parser.
        const auto engineRoot = std::filesystem::path(reinterpret_cast<const char8_t*>(__FILE__)).parent_path().parent_path().parent_path().parent_path();
        const auto sdkInclude = engineRoot / "Build" / "ScriptSdk" / "2D" / "Include";
        const auto encoded = (sdkInclude / std::filesystem::path(reinterpret_cast<const char8_t*>(sdkRelative))).generic_u8string();
        String uri = "file:///";
        constexpr char hex[] = "0123456789ABCDEF";
        for (const auto character : encoded)
        {
            const unsigned char value = static_cast<unsigned char>(character);
            if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') || value == '/' || value == ':' || value == '-' || value == '_' || value == '.' || value == '~')
            {
                uri.push_back(static_cast<char>(value));
            }
            else
            {
                uri.push_back('%');
                uri.push_back(hex[value >> 4]);
                uri.push_back(hex[value & 15]);
            }
        }
        return uri;
    }
    // 닻 글자의 64 비트 FNV-1a 다. **`JBroScriptSdk` 의 `LineAnchors` 와 같은 셈이다** - 앞뒤 공백을 떼고 바이트를 섞는다. 둘을 함께 고친다.
    static UInt64 AnchorHash(const char* anchor)
    {
        std::size_t first = 0;
        std::size_t last = std::strlen(anchor);
        while (first < last && (anchor[first] == ' ' || anchor[first] == '\t'))
        {
            ++first;
        }
        while (last > first && (anchor[last - 1] == ' ' || anchor[last - 1] == '\t'))
        {
            --last;
        }
        std::uint64_t hash = 14695981039346656037ull;
        for (std::size_t at = first; at < last; ++at)
        {
            hash ^= static_cast<unsigned char>(anchor[at]);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    // **정의 자리는 선언의 글자로 찾는다**(D-340). 전에는 원본 헤더의 줄 번호를 손으로 적어 두어, 헤더에 줄 하나를 끼우면 그 아래 자리가 모두 밀렸다 -
    // 주석 줄에 떨어지면 빌드가 멈추고, 다른 코드 줄에 떨어지면 아무 말 없이 엉뚱한 곳을 가리켰다. 이제 사본 줄 가운데 코드 글자가 `anchor` 와 같은
    // 것의 `occurrence` 번째(0 부터)를 찾고, 그 줄의 앞 공백 뒤 `offset` 바이트가 이름이다. 찾지 못하면 그 항목을 말하며 멈춘다 - 선언이 바뀌었다는 뜻이다.
    template<class T> static void DefineValue(T& value, const char* header, const char* anchor, UInt32 occurrence, UInt32 offset, UInt32 length)
    {
        const ScriptSdk::HeaderMap* map = nullptr;
        for (const auto& candidate : ScriptSdk::Headers)
        {
            if (std::strcmp(candidate.Source, header) == 0)
            {
                map = &candidate;
                break;
            }
        }
        if (map == nullptr)
        {
            throw std::runtime_error(std::string("script API definition names a header that is not in the script SDK: ") + header);
        }
        const UInt64 hash = AnchorHash(anchor);
        UInt32 seen = 0;
        UInt32 sdkLine = 0;
        for (UInt32 line = 0; line < map->LineCount && sdkLine == 0; ++line)
        {
            if (map->Hashes[line.Get()] == hash)
            {
                if (seen == occurrence)
                {
                    sdkLine = line + 1;
                }
                ++seen;
            }
        }
        if (sdkLine == 0)
        {
            throw std::runtime_error(std::string("script API definition is not in the script SDK - the declaration changed: ") + header + ": " + anchor);
        }
        const UInt32 column = map->Indents[sdkLine.Get() - 1] + offset + 1;
        const UInt32 start = map->Starts[sdkLine.Get() - 1] + column - 1;
        value.DefinitionUri = HeaderUri(map->Sdk);
        value.DefinitionRange.Begin = {start,sdkLine,column};
        value.DefinitionRange.End = {start+length,sdkLine,column+length};
    }
    void Define(ApiType& value, const char* header, const char* anchor, UInt32 occurrence, UInt32 offset, UInt32 length)
    {
        DefineValue(value,header,anchor,occurrence,offset,length);
    }
    void Define(ApiMember& value, const char* header, const char* anchor, UInt32 occurrence, UInt32 offset, UInt32 length)
    {
        DefineValue(value,header,anchor,occurrence,offset,length);
    }
    ApiType& AddType(ApiCatalog& catalog, const char* name, const char* native, ApiTypeKind kind)
    {
        ApiType value;
        value.Name = name;
        value.NativeName = native;
        value.Kind = kind;
        catalog.Types.Add(std::move(value));
        return catalog.Types.Last();
    }
    ApiType& FindType(ApiCatalog& catalog, const char* name)
    {
        for (auto& type : catalog.Types)
        {
            if (type.Name == name)
            {
                return type;
            }
        }
        throw std::runtime_error("catalog declaration references an unregistered type");
    }
    void AddEnums(ApiCatalog& catalog);
}

namespace JBro::ScriptApiCatalog2D
{
    ScriptCompiler::ApiCatalog BuildCatalog()
    {
        ScriptCompiler::ApiCatalog catalog;
        Internal::AddEnums(catalog);
        Internal::AddValues(catalog);
        Internal::AddHandles(catalog);
        Internal::AddServices(catalog);
        Internal::AddCallbacks(catalog);
        Internal::AddContainers(catalog);
        Internal::AddValueMembers(catalog);
        Internal::AddGenericMembers(catalog);
        Internal::AddFunctions(catalog);
        Internal::ApplyDefinitions(catalog);
        Internal::ValidateCatalog(catalog);
        return catalog;
    }
}
