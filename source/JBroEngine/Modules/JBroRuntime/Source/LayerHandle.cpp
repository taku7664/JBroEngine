#include <JBro/Runtime/LayerHandle.h>

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
        std::fprintf(stderr, "JBro warning: Layer::%s on invalid handle (id=%u)\n", operation, static_cast<unsigned>(id));
    }
}
