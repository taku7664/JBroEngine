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
    // 원본 헤더의 줄·열을 사본의 줄·열·바이트로 옮긴다. 사본은 주석만 있던 줄을 빼고 코드 앞의 주석은 같은 폭의 공백으로 두므로 열은 그대로다.
    template<class T> static void DefineValue(T& value, const char* header, UInt32 line, UInt32 column, UInt32 length)
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
        if (map == nullptr || line == 0 || line > map->LineCount || map->Lines[line-1] == 0)
        {
            throw std::runtime_error(std::string("script API definition is not in the script SDK: ") + header + ":" + std::to_string(line));
        }
        const UInt32 sdkLine = map->Lines[line-1];
        const UInt32 offset = map->Starts[sdkLine-1] + column - 1;
        value.DefinitionUri = HeaderUri(map->Sdk);
        value.DefinitionRange.Begin = {offset,sdkLine,column};
        value.DefinitionRange.End = {offset+length,sdkLine,column+length};
    }
    void Define(ApiType& value, const char* header, UInt32 line, UInt32 column, UInt32 length)
    {
        DefineValue(value,header,line,column,length);
    }
    void Define(ApiMember& value, const char* header, UInt32 line, UInt32 column, UInt32 length)
    {
        DefineValue(value,header,line,column,length);
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
