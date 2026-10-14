#include "CatalogBuilder.h"
namespace JBro::ScriptApiCatalog2D::Internal
{
namespace
{
Bool KnownType(const ApiCatalog& catalog, String name)
{
    name = name.Trimmed();
    if (name.StartsWith("ref "))
    {
        name = name.Substr(4).Trimmed();
    }
    if (name == "Int" || name == "Float" || name == "Bool" || name == "String" || name == "Void" || name == "T" || name == "K" || name == "V")
    {
        return true;
    }
    const auto open = name.find('<');
    if (open != String::npos)
    {
        if (!name.EndsWith(">"))
        {
            return false;
        }
        const String base = name.Substr(0,open).Trimmed();
        if (base != "Array" && base != "Table")
        {
            return false;
        }
        const String arguments = name.Substr(open+1,name.size()-open-2);
        std::size_t depth = 0;
        std::size_t first = 0;
        std::size_t count = 0;
        for (std::size_t index = 0; index <= arguments.size(); ++index)
        {
            const char character = index < arguments.size() ? arguments[index] : ',';
            if (character == '<')
            {
                ++depth;
            }
            else if (character == '>')
            {
                if (depth == 0)
                {
                    return false;
                }
                --depth;
            }
            else if (character == ',' && depth == 0)
            {
                if (!KnownType(catalog,arguments.Substr(first,index-first)))
                {
                    return false;
                }
                first = index+1;
                ++count;
            }
        }
        return depth == 0 && count == (base == "Array" ? 1u : 2u);
    }
    for (const auto& type : catalog.Types)
    {
        if (type.Name == name)
        {
            return true;
        }
    }
    return false;
}
void ValidateMember(const ApiCatalog& catalog, const ApiMember& member)
{
    if (!KnownType(catalog,member.Type))
    {
        throw std::runtime_error("public API return/field type is unregistered: " + member.Name.Std() + ": " + member.Type.Std());
    }
    for (const auto& parameter : member.Parameters)
    {
        if (!KnownType(catalog,parameter.Type))
        {
            throw std::runtime_error("public API parameter type is unregistered: " + member.Name.Std() + ": " + parameter.Type.Std());
        }
    }
    if (member.HasOutputProjection != (member.Projection != ApiProjectionKind::None))
    {
        throw std::runtime_error("public API projection flag disagrees with projection kind");
    }
    for (UInt32 output : member.NativeOutputParameters)
    {
        if (output >= member.NativeParameterNames.Size())
        {
            throw std::runtime_error("public API output projection index exceeds original C++ signature");
        }
    }
}
}
void ValidateCatalog(const ApiCatalog& catalog)
{
    for (std::size_t index = 0; index < catalog.Types.Size(); ++index)
    {
        const auto& type = catalog.Types[index];
        for (std::size_t other = index+1; other < catalog.Types.Size(); ++other)
        {
            if (catalog.Types[other].Name == type.Name)
            {
                throw std::runtime_error("duplicate public API type declaration");
            }
        }
        for (const auto& member : type.Members)
        {
            ValidateMember(catalog,member);
        }
    }
    for (const auto& member : catalog.Functions)
    {
        ValidateMember(catalog,member);
    }
    for (const auto& global : catalog.Globals)
    {
        if (!KnownType(catalog,global.Type))
        {
            throw std::runtime_error("public API global type is unregistered");
        }
    }
}
}
