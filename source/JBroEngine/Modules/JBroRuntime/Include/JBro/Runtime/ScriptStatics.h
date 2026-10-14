#pragma once

#include <JBro/Runtime/ScriptGuard.h>

#include <cstddef>
#include <memory>
#include <new>
#include <utility>

namespace JBro
{
    enum class ScriptStaticState { Uninitialized, Initializing, Ready };

    class ScriptSchemaRegistration final
    {
    public:
        explicit ScriptSchemaRegistration(void (*initialize)()) noexcept;
        ScriptSchemaRegistration(const ScriptSchemaRegistration&) = delete;
        ScriptSchemaRegistration& operator=(const ScriptSchemaRegistration&) = delete;

    private:
        friend void InitializePendingScriptSchemas();
        void (*m_initialize)() = nullptr;
        ScriptSchemaRegistration* m_next = nullptr;
    };

    void InitializePendingScriptSchemas();

    // Registration itself only links constant callback data. Values are built
    // after host contexts and type tables are bound, before user callbacks run.
    class ScriptStaticRegistration final
    {
    public:
        using InitializeFunction = void (*)();
        using DestroyFunction = void (*)() noexcept;

        ScriptStaticRegistration(InitializeFunction initialize, DestroyFunction destroy) noexcept;
        ScriptStaticRegistration(const ScriptStaticRegistration&) = delete;
        ScriptStaticRegistration& operator=(const ScriptStaticRegistration&) = delete;
        void MarkInitialized() noexcept;

    private:
        friend void InitializePendingScriptStatics();
        friend void DestroyScriptStatics() noexcept;
        InitializeFunction m_initialize = nullptr;
        DestroyFunction m_destroy = nullptr;
        ScriptStaticRegistration* m_pendingNext = nullptr;
        ScriptStaticRegistration* m_initializedNext = nullptr;
        Bool m_initialized = false;
    };

    void InitializePendingScriptStatics();
    void DestroyScriptStatics() noexcept;

    template<typename T>
    class ScriptStaticValue final
    {
    public:
        ScriptStaticValue() = default;
        ScriptStaticValue(const ScriptStaticValue&) = delete;
        ScriptStaticValue& operator=(const ScriptStaticValue&) = delete;

        template<typename Factory>
        void Construct(Factory&& factory)
        {
            std::construct_at(reinterpret_cast<T*>(m_storage), std::forward<Factory>(factory)());
            m_active = true;
        }

        T& Get() noexcept
        {
            return *std::launder(reinterpret_cast<T*>(m_storage));
        }

        void Destroy() noexcept
        {
            if (m_active)
            {
                std::destroy_at(std::launder(reinterpret_cast<T*>(m_storage)));
                m_active = false;
            }
        }

    private:
        alignas(T) std::byte m_storage[sizeof(T)]{};
        Bool m_active = false;
    };
}
