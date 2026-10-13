#include <JBro/Runtime/ObjectService.h>

#include <JBro/Runtime/IObjectSystem.h>
#include <JBro/Runtime/SystemContext.h>

namespace JBro::Service
{
    namespace
    {
        System::IObjectSystem* Objects()
        {
            return GetSystemContext().Objects;
        }
    }

    Handle::GameObject ObjectService::Create(const char* name, Handle::GameObject parent, Handle::Layer layer) const
    {
        System::IObjectSystem* objects = Objects();
        if (objects == nullptr)
        {
            return {};
        }
        return objects->Create(name, parent, layer.GetId());
    }

    Handle::GameObject ObjectService::Find(const char* name) const
    {
        return Find(MakeNameId(name));
    }

    Handle::GameObject ObjectService::Find(NameId name) const
    {
        System::IObjectSystem* objects = Objects();
        if (objects == nullptr || name == InvalidNameId)
        {
            return {};
        }
        return objects->Find(name, false);
    }

    void ObjectService::FindAll(const char* name, Array<Handle::GameObject>& results) const
    {
        results.Clear();
        System::IObjectSystem* objects = Objects();
        const NameId id = MakeNameId(name);
        if (objects == nullptr || id == InvalidNameId)
        {
            return;
        }
        objects->FindAll(id, false, results);
    }

    Handle::GameObject ObjectService::FindWithTag(const char* tag) const
    {
        System::IObjectSystem* objects = Objects();
        const NameId id = MakeNameId(tag);
        if (objects == nullptr || id == InvalidNameId)
        {
            return {};
        }
        return objects->Find(id, true);
    }

    void ObjectService::FindAllWithTag(const char* tag, Array<Handle::GameObject>& results) const
    {
        results.Clear();
        System::IObjectSystem* objects = Objects();
        const NameId id = MakeNameId(tag);
        if (objects == nullptr || id == InvalidNameId)
        {
            return;
        }
        objects->FindAll(id, true, results);
    }

    Handle::Layer ObjectService::GetLayer(const char* name) const
    {
        System::IObjectSystem* objects = Objects();
        const NameId id = MakeNameId(name);
        if (objects == nullptr || id == InvalidNameId)
        {
            return {};
        }
        return Handle::LayerAccess::FromId(objects->FindLayer(id));
    }
}
