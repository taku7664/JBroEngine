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

    Vector2 CanvasViewportSystem::GetOriginPixels(UInt32 index)
    {
        const CanvasViewport* viewport = At(m_canvas, index);
        return viewport != nullptr ? viewport->originPixels : Vector2{};
    }

    void CanvasViewportSystem::SetOriginPixels(UInt32 index, Vector2 pixels)
    {
        if (CanvasViewport* viewport = At(m_canvas, index))
        {
            viewport->originPixels = pixels;
        }
    }

    Vector2 CanvasViewportSystem::GetSizePixels(UInt32 index)
    {
        const CanvasViewport* viewport = At(m_canvas, index);
        return viewport != nullptr ? viewport->sizePixels : Vector2{};
    }

    void CanvasViewportSystem::SetSizePixels(UInt32 index, Vector2 pixels)
    {
        if (CanvasViewport* viewport = At(m_canvas, index))
        {
            viewport->sizePixels = pixels;
        }
    }

    AssetHandle CanvasViewportSystem::GetRenderTexture(UInt32 index)
    {
        const CanvasViewport* viewport = At(m_canvas, index);
        return viewport != nullptr ? viewport->renderTexture : AssetHandle{};
    }

    void CanvasViewportSystem::SetRenderTexture(UInt32 index, AssetHandle texture)
    {
        CanvasViewport* viewport = At(m_canvas, index);
        if (viewport == nullptr)
        {
            return;
        }
        viewport->renderTexture = texture;
        // 빈 핸들은 "화면으로" 다. 아이디를 남기면 풀리지 않은 렌더 텍스처 뷰포트로 보아 그리지 않는다(`DrawsToTexture`).
        if (texture.generation == 0)
        {
            viewport->renderTextureId = Uuid{};
        }
    }

    Bool CanvasViewportSystem::DrawsLayer(UInt32 index, UInt32 layer)
    {
        const CanvasViewport* viewport = At(m_canvas, index);
        return viewport != nullptr && ViewportDrawsLayer(*m_canvas, *viewport, static_cast<LayerId>(layer));
    }

    void CanvasViewportSystem::SetLayerDrawn(UInt32 index, UInt32 layerId, Bool drawn)
    {
        CanvasViewport* viewport = At(m_canvas, index);
        const Layer* layer = m_canvas != nullptr ? m_canvas->FindLayer(static_cast<LayerId>(layerId)) : nullptr;
        if (viewport == nullptr || layer == nullptr)
        {
            return;
        }
        const LayerId id = layer->GetId();
        const auto contains = [&](LayerId wanted) {
            for (std::size_t at = 0; at < viewport->layers.Size(); ++at)
            {
                if (viewport->layers[at] == wanted)
                {
                    return true;
                }
            }
            return false;
        };
        // 화면 레이어는 목록에 든 것만 이 뷰포트에 그린다 - 넣고 빼면 그만이다.
        if (layer->GetSpace() == LayerSpace::Screen)
        {
            if (drawn && false == contains(id))
            {
                viewport->layers.Add(id);
            }
            else if (false == drawn)
            {
                viewport->layers.RemoveAll([id](LayerId entry) { return entry == id; });
            }
            return;
        }
        // 월드 레이어: 목록에 월드 쪽이 없으면 "모두" 다. 그때 하나를 끄면 나머지 월드 레이어를 모두 적는다.
        ViewLayerMask world;
        ViewLayerMask screen;
        BuildViewportLayerMasks(*m_canvas, *viewport, world, screen);
        if (drawn == world.Draws(layer->GetOrder()))
        {
            return;
        }
        if (false == world.filtered)
        {
            for (std::size_t at = 0; at < m_canvas->GetLayerCount(); ++at)
            {
                const Layer* other = m_canvas->GetLayerAt(at);
                if (other != nullptr && other->GetSpace() != LayerSpace::Screen && other->GetId() != id)
                {
                    viewport->layers.Add(other->GetId());
                }
            }
        }
        else if (drawn)
        {
            viewport->layers.RemoveAll([](LayerId entry) { return entry == InvalidLayerId; });
            viewport->layers.Add(id);
        }
        else
        {
            viewport->layers.RemoveAll([id](LayerId entry) { return entry == id; });
        }
        // 월드 쪽이 하나도 남지 않으면 "월드 레이어 없음" 표시를 둔다 - 비면 "모두" 로 뒤집힌다.
        BuildViewportLayerMasks(*m_canvas, *viewport, world, screen);
        if (false == world.filtered)
        {
            viewport->layers.Add(InvalidLayerId);
        }
    }

    void CanvasViewportSystem::ResetLayers(UInt32 index)
    {
        if (CanvasViewport* viewport = At(m_canvas, index))
        {
            viewport->layers.Clear();
        }
    }
}
