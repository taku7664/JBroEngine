#pragma once

#include <JBro/Runtime/ScriptErrorContext.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Runtime/Nullable.h>

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
