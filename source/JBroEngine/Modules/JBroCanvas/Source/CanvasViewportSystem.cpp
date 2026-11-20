#include <JBro/Canvas/CanvasViewportSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>

#include <cstring>

namespace JBro
{
    namespace
    {
        CanvasViewport* At(Canvas* canvas, UInt32 index)
        {
            return canvas != nullptr ? canvas->GetViewportAt(static_cast<std::size_t>(index)) : nullptr;
        }
    }

    void CanvasViewportSystem::Bind(Canvas* canvas)
    {
        m_canvas = canvas;
    }

    UInt32 CanvasViewportSystem::GetCount()
    {
        return m_canvas != nullptr ? static_cast<JBro::UInt32>(m_canvas->GetViewportCount()) : UInt32(0);
    }

    Int32 CanvasViewportSystem::Find(const char* name)
    {
        if (m_canvas == nullptr || name == nullptr)
        {
            return -1;
        }
        for (std::size_t index = 0; index < m_canvas->GetViewportCount(); ++index)
        {
            if (std::strcmp(m_canvas->GetViewportAt(index)->name.c_str(), name) == 0)
            {
                return static_cast<JBro::Int32>(index);
            }
        }
        return -1;
    }

    Bool CanvasViewportSystem::IsActive(UInt32 index)
    {
        const CanvasViewport* viewport = At(m_canvas, index);
        return viewport != nullptr && viewport->active;
    }

    void CanvasViewportSystem::SetActive(UInt32 index, Bool active)
    {
        if (CanvasViewport* viewport = At(m_canvas, index))
        {
            viewport->active = active;
        }
    }

    Vector2 CanvasViewportSystem::GetOrigin(UInt32 index)
    {
        const CanvasViewport* viewport = At(m_canvas, index);
        return viewport != nullptr ? viewport->origin : Vector2{};
    }

    void CanvasViewportSystem::SetOrigin(UInt32 index, Vector2 origin)
    {
        if (CanvasViewport* viewport = At(m_canvas, index))
        {
            viewport->origin = origin;
        }
    }

    Vector2 CanvasViewportSystem::GetSize(UInt32 index)
    {
        const CanvasViewport* viewport = At(m_canvas, index);
        return viewport != nullptr ? viewport->size : Vector2{};
    }

    void CanvasViewportSystem::SetSize(UInt32 index, Vector2 size)
    {
        if (CanvasViewport* viewport = At(m_canvas, index))
        {
            viewport->size = size;
        }
    }

    Bool CanvasViewportSystem::GetClearViewport(UInt32 index)
    {
        const CanvasViewport* viewport = At(m_canvas, index);
        return viewport != nullptr && viewport->clearViewport;
    }

    void CanvasViewportSystem::SetClearViewport(UInt32 index, Bool clear)
    {
        if (CanvasViewport* viewport = At(m_canvas, index))
        {
            viewport->clearViewport = clear;
        }
    }

    Handle::GameObject CanvasViewportSystem::GetCamera(UInt32 index)
    {
        const CanvasViewport* viewport = At(m_canvas, index);
        if (viewport == nullptr || false == viewport->HasCamera())
        {
            return {};
        }
        return Internal::GameObjectHandleAccess::FromId(viewport->camera);
    }

    Bool CanvasViewportSystem::SetCamera(UInt32 index, const Handle::GameObject& camera)
    {
        CanvasViewport* viewport = At(m_canvas, index);
        Object::GameObject* object = Internal::GameObjectHandleAccess::Resolve(camera);
        if (viewport == nullptr || object == nullptr)
        {
            return false;
        }
        viewport->camera = object->GetInstanceId();
        return true;
    }

    void CanvasViewportSystem::SetAutoCamera(UInt32 index)
    {
        if (CanvasViewport* viewport = At(m_canvas, index))
        {
            viewport->camera = InvalidInstanceId;
        }
    }
}
