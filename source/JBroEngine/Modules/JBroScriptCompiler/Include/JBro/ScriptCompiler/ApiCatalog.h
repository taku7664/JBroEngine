#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/String.h>
#include <JBro/ScriptCompiler/SourceText.h>

namespace JBro::ScriptCompiler
{
    enum class ApiTypeKind : unsigned char { Value, Handle, Script, Class, Interface, Enum, Service };
    enum class ApiMemberKind : unsigned char { Field, Function, Constructor };
    enum class ApiProjectionKind : unsigned char { None, ValueReturn, NullableReturn, CollectionReturn, StringReturn };

    struct ParameterInfo
    {
        String Name;
        String Type;
        Bool Borrowed = false;
        Bool HasDefault = false;
        String DefaultValue;
    };

    struct ApiMember
    {
        String Name;
        String NativeName;
        ApiMemberKind Kind = ApiMemberKind::Function;
        String Type;
        Array<ParameterInfo> Parameters;
        Array<String> TypeParameters;
        Bool IsStatic = false;
        Bool Writable = false;
        Bool IsCallback = false;
        Bool MutatesReceiver = false;
        Bool MayInvalidateHandles = false;
        Bool HasOutputProjection = false;
        ApiProjectionKind Projection = ApiProjectionKind::None;
        Array<String> NativeParameterNames;
        Array<UInt32> NativeOutputParameters;
        Bool SuccessFromNativeReturn = false;
        String NativeReturnField;
        String NativeSignature;
        String Description;
        String DefinitionUri;
        SourceRange DefinitionRange;
    };

    struct ApiType
    {
        String Name;
        String NativeName;
        ApiTypeKind Kind = ApiTypeKind::Value;
        Array<String> Bases;
        Array<ApiMember> Members;
        Array<String> EnumValues;
        String DefinitionUri;
        SourceRange DefinitionRange;
    };

    struct ApiGlobal
    {
        String Name;
        String Type;
        String NativeExpression;
    };

    struct ApiCatalog
    {
        Array<ApiType> Types;
        Array<ApiGlobal> Globals;
        Array<ApiMember> Functions;
    };
}
