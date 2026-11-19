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
        // The native output's element type is not what the script sees (Array<std::byte> shown as Array<Int>).
        // The emitter cannot declare that output, so CanEmitOutputProjection refuses it (D-382).
        Bool NativeOutputDiffers = false;
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

    // 출력 인자로 투영한 엔진 함수를 C++ 생성기가 원래 호출로 되돌릴 수 있는가(D-382). 분석과 생성기가 이 하나를 같이 써서,
    // 생성기가 내리지 못하는 꼴은 분석이 편집 중에 `JBC3050` 으로 거절한다 - 전에는 분석·언어 서버가 통과시키고 빌드에서야 멈췄다.
    // 투영이 없는 함수는 참이다. 구현은 생성기(`CppEmitter.cpp`)에 있다.
    Bool CanEmitOutputProjection(const struct ApiMember& member) noexcept;

    struct ApiCatalog
    {
        Array<ApiType> Types;
        Array<ApiGlobal> Globals;
        Array<ApiMember> Functions;
    };
}
