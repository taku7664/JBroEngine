#pragma once
#include "NativeTypes.h"

namespace JBro::ScriptApiCatalog2D::Internal
{
    using namespace ScriptCompiler;
    void Define(ApiType& type, const char* header, UInt32 line, UInt32 column, UInt32 length);
    void Define(ApiMember& member, const char* header, UInt32 line, UInt32 column, UInt32 length);
    ApiType& AddType(ApiCatalog& catalog, const char* name, const char* native, ApiTypeKind kind);
    ApiType& FindType(ApiCatalog& catalog, const char* name);
    void AddValueMembers(ApiCatalog& catalog);
    void AddGenericMembers(ApiCatalog& catalog);
    void ValidateCatalog(const ApiCatalog& catalog);
    void ApplyDefinitions(ApiCatalog& catalog);
    void AddFunctions(ApiCatalog& catalog);
    void AddHandles(ApiCatalog& catalog);
    void AddServices(ApiCatalog& catalog);
    void AddValues(ApiCatalog& catalog);
    void AddCallbacks(ApiCatalog& catalog);
    void AddContainers(ApiCatalog& catalog);
}
