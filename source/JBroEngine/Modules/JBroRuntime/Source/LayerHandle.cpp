#include <JBro/Runtime/LayerHandle.h>
#include <JBro/Runtime/ScriptLogContext.h>

#include <JBro/Runtime/IObjectSystem.h>
#include <JBro/Runtime/SystemContext.h>

#include <cstdio>

namespace JBro::Handle
{
    namespace
    {
        System::IObjectSystem* Objects()
        {
            return GetSystemContext().Objects;
        }
    }

    Bool Layer::IsValid() const
    {
        System::IObjectSystem* objects = Objects();
        return objects != nullptr && m_id != System::IObjectSystem::InvalidLayer && objects->IsLayerAlive(m_id);
    }

    Layer::operator bool() const
    {
        return IsValid();
    }

    const char* Layer::GetName() const
    {
        if (false == IsValid())
        {
            ReportInvalidAccess("GetName", m_id);
            return "";
        }
        return Objects()->GetLayerName(m_id);
    }

    GameObject Layer::operator[](const char* name) const
    {
        return (*this)[MakeNameId(name)];
    }

    GameObject Layer::operator[](NameId name) const
    {
        if (false == IsValid())
        {
            ReportInvalidAccess("operator[]", m_id);
            return {};
        }
        if (name == InvalidNameId)
        {
            return {};
        }
        return Objects()->FindRoot(m_id, name);
    }

    UInt32 Layer::GetId() const
    {
        return m_id;
    }

    Bool Layer::operator==(const Layer& other) const
    {
        return m_id == other.m_id;
    }

    void Layer::ReportInvalidAccess(const char* operation, UInt32 id)
    {
        // `Handle::GameObject` 의 무효 접근과 같은 길이다(D-380).
        char key[96];
        char message[160];
        std::snprintf(key, sizeof(key), "handle.Layer.%s", operation);
        std::snprintf(message, sizeof(message), "Layer::%s on invalid handle (id=%u)", operation, static_cast<unsigned>(id.Get()));
        WriteEngineLog(LogLevel::Warning, "script", key, message);
    }
}
