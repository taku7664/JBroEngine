#pragma once

#include <JBro/Runtime/ScriptErrorContext.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Runtime/Nullable.h>
#include <JBro/Types/String.h>

#include <string_view>

namespace JBro::ScriptGuard
{
    struct AbortCallback final {};

    template<typename T>
    const T& RequireHandle(const T& handle, const char* siteKey, const char* message)
    {
        if (!handle.IsValid())
        {
            ReportScriptError(siteKey, message);
            throw AbortCallback{};
        }
        return handle;
    }

    // Lowered member access stores its receiver in one local, then calls one
    // of these overloads. The returned reference is used inside that scope.
    template<typename T>
    T& Require(Nullable<T>& value, const char* siteKey, const char* message)
    {
        if (!value.HasValue())
        {
            ReportScriptError(siteKey, message);
            throw AbortCallback{};
        }
        return value.Value();
    }

    template<typename T>
    T& Require(Ref<T>& reference, const char* siteKey, const char* message)
    {
        T* object = reference.Get();
        if (object == nullptr)
        {
            ReportScriptError(siteKey, message);
            throw AbortCallback{};
        }
        return *object;
    }

    template<typename T>
    T& Require(T& handle, const char* siteKey, const char* message)
    {
        if (!handle.IsValid())
        {
            ReportScriptError(siteKey, message);
            throw AbortCallback{};
        }
        return handle;
    }

    // 엔진 함수에 넘기는 스크립트 `String`·`Int` 다(D-383). 카탈로그는 엔진의 `const char*`·`UInt32`·`NameId` 매개변수를 스크립트에
    // `String`·`Int` 로 보이므로, 변수를 그대로 넘기면 C2664 였다(리터럴만 됐다). 이 대리자는 그 몇 가지로만 바뀐다 -
    // 다른 타입의 매개변수에는 맞지 않으므로 오버로드 고르기를 바꾸지 않는다. 대리자는 호출 식 안에서만 산다.
    struct StringArgument
    {
        const String& Value;

        operator const char*() const noexcept { return Value.c_str(); }
        operator const String&() const noexcept { return Value; }
        operator std::string_view() const noexcept { return std::string_view(Value); }
    };

    template<typename S>
    struct IntegerArgument
    {
        IntegerType<S> Value;

        template<typename U>
        operator IntegerType<U>() const noexcept { return IntegerType<U>(static_cast<U>(Value.Get())); }
    };

    inline StringArgument Argument(const String& value) noexcept { return {value}; }

    template<typename S>
    IntegerArgument<S> Argument(IntegerType<S> value) noexcept { return {value}; }

    // Literals stay raw in generated code ("slot", 1): a string literal already converts to every string parameter,
    // and an integer literal becomes an Int argument.
    inline const char* Argument(const char* value) noexcept { return value; }

    template<typename I, std::enable_if_t<std::is_integral_v<I>, int> = 0>
    IntegerArgument<std::int64_t> Argument(I value) noexcept { return {IntegerType<std::int64_t>(static_cast<std::int64_t>(value))}; }

    template<typename T>
    Bool IsPresent(const Nullable<T>& value) noexcept { return value.HasValue(); }

    template<typename T>
    Bool IsPresent(const Ref<T>& value) { return value.Get() != nullptr; }

    template<typename T>
    Bool IsPresent(const T& handle) { return handle.IsValid(); }

    template<typename T>
    T& UnwrapPresent(Nullable<T>& value) noexcept { return value.Value(); }

    template<typename T>
    T& UnwrapPresent(Ref<T>& value) { return *value.Get(); }

    template<typename T>
    T& UnwrapPresent(T& handle) noexcept { return handle; }
}
