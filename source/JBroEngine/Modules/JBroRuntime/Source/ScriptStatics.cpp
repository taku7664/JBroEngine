#include <JBro/Runtime/ScriptStatics.h>

namespace JBro
{
    namespace
    {
        ScriptStaticRegistration* g_pending = nullptr;
        ScriptStaticRegistration* g_initialized = nullptr;
        ScriptSchemaRegistration* g_schemas = nullptr;
    }

    ScriptSchemaRegistration::ScriptSchemaRegistration(void (*initialize)()) noexcept
        : m_initialize(initialize), m_next(g_schemas)
    {
        g_schemas = this;
    }

    void InitializePendingScriptSchemas()
    {
        for (ScriptSchemaRegistration* entry = g_schemas; entry != nullptr; entry = entry->m_next)
        {
            entry->m_initialize();
        }
    }

    ScriptStaticRegistration::ScriptStaticRegistration(InitializeFunction initialize, DestroyFunction destroy) noexcept
        : m_initialize(initialize), m_destroy(destroy), m_pendingNext(g_pending)
    {
        g_pending = this;
    }

    void ScriptStaticRegistration::MarkInitialized() noexcept
    {
        if (!m_initialized)
        {
            m_initialized = true;
            m_initializedNext = g_initialized;
            g_initialized = this;
        }
    }

    void InitializePendingScriptStatics()
    {
        for (ScriptStaticRegistration* entry = g_pending; entry != nullptr; entry = entry->m_pendingNext)
        {
            entry->m_initialize();
        }
    }

    void DestroyScriptStatics() noexcept
    {
        while (g_initialized != nullptr)
        {
            ScriptStaticRegistration* entry = g_initialized;
            g_initialized = entry->m_initializedNext;
            entry->m_initialized = false;
            entry->m_initializedNext = nullptr;
            entry->m_destroy();
        }
    }
}
