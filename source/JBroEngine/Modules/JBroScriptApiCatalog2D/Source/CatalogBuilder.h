#pragma once
#include "NativeTypes.h"

namespace JBro::ScriptApiCatalog2D::Internal
{
    using namespace ScriptCompiler;
    // 정의 자리는 선언의 글자(닻)로 적는다(D-340, Catalog.cpp). `offset` 은 닻 안에서 이름이 시작하는 바이트다.
    void Define(ApiType& type, const char* header, const char* anchor, UInt32 occurrence, UInt32 offset, UInt32 length);
    void Define(ApiMember& member, const char* header, const char* anchor, UInt32 occurrence, UInt32 offset, UInt32 length);
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
