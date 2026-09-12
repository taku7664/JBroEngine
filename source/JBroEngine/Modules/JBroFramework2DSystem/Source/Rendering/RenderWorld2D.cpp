#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>

#include <algorithm>
#include <new>

namespace JBro
{
    bool RenderWorld2D::ReserveSprites(std::size_t capacity)
    {
        try
        {
            m_sprites.Reserve(capacity);
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        return true;
    }

    void RenderWorld2D::BeginFrame()
    {
        m_camera = {};
        m_hasCamera = false;
        m_sprites.Clear();
        m_droppedSpriteCount = 0;
    }

    void RenderWorld2D::SetCamera(const RenderCamera2D& camera)
    {
        m_camera = camera;
        m_hasCamera = true;
    }

    bool RenderWorld2D::SubmitSprite(const SpriteRenderItem& item)
    {
        if (m_sprites.Size() == m_sprites.Capacity())
        {
            ++m_droppedSpriteCount;
            return false;
        }
        m_sprites.Add(item);
        return true;
    }

    void RenderWorld2D::Sort()
    {
        if (m_sprites.Size() < 2)
        {
            return;
        }
        // 레이어 합성 순서가 가장 바깥이다. 같은 레이어 안에서만 renderOrder 가 의미를 갖고,
        // 그마저 같으면 생성 시각 순인 sourceId 로 안정 정렬한다.
        std::sort(
            m_sprites.begin(),
            m_sprites.end(),
            [](const SpriteRenderItem& left, const SpriteRenderItem& right)
        {
            if (left.layerOrder != right.layerOrder)
            {
                return left.layerOrder < right.layerOrder;
            }
            if (left.renderOrder != right.renderOrder)
            {
                return left.renderOrder < right.renderOrder;
            }

            return left.sourceId < right.sourceId;
        });
    }

    void RenderWorld2D::EndFrame()
    {
        Sort();
    }

    const RenderCamera2D* RenderWorld2D::GetCamera() const
    {
        if (false == m_hasCamera)
        {
            return nullptr;
        }
        return &m_camera;
    }

    std::size_t RenderWorld2D::GetSpriteCount() const
    {
        return m_sprites.Size();
    }

    std::size_t RenderWorld2D::GetSpriteCapacity() const
    {
        return m_sprites.Capacity();
    }

    std::size_t RenderWorld2D::GetDroppedSpriteCount() const
    {
        return m_droppedSpriteCount;
    }

    const SpriteRenderItem* RenderWorld2D::GetSprites() const
    {
        return m_sprites.Data();
    }
}
