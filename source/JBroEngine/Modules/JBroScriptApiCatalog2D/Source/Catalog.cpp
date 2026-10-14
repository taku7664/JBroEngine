#include <JBro/ScriptApiCatalog2D/Catalog.h>
#include "CatalogBuilder.h"
#include <filesystem>

namespace JBro::ScriptApiCatalog2D::Internal
{
    static String HeaderUri(const char* relative)
    {
        // /FC supplies a stable absolute source path; this is metadata, never a header parser.
        const auto sourceRoot = std::filesystem::path(reinterpret_cast<const char8_t*>(__FILE__)).parent_path().parent_path().parent_path().parent_path().parent_path();
        const auto encoded = (sourceRoot / std::filesystem::path(reinterpret_cast<const char8_t*>(relative))).generic_u8string();
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
    template<class T> static void DefineValue(T& value, const char* header, UInt32 offset, UInt32 line, UInt32 column, UInt32 length)
    {
        value.DefinitionUri = HeaderUri(header);
        value.DefinitionRange.Begin = {offset,line,column};
        value.DefinitionRange.End = {offset+length,line,column+length};
    }
    void Define(ApiType& value, const char* header, UInt32 offset, UInt32 line, UInt32 column, UInt32 length)
    {
        DefineValue(value,header,offset,line,column,length);
    }
    void Define(ApiMember& value, const char* header, UInt32 offset, UInt32 line, UInt32 column, UInt32 length)
    {
        DefineValue(value,header,offset,line,column,length);
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
