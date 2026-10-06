#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>

#include <algorithm>
#include <new>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    Bool RenderWorld2D::ReserveSprites(std::size_t capacity)
    {
        try
        {
            m_sprites.Reserve(capacity);
            // 정렬 키도 같이 잡는다. Sort 가 프레임 중에 할당하면 안 된다.
            m_order.Reserve(capacity);
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
        m_unusableCameras = 0;
        m_sprites.Clear();
        m_order.Clear();
        m_droppedSpriteCount = 0;
        m_screenSprites = 0;
        m_lights.Clear();
        m_droppedLightCount = 0;
        m_shadowEdges.Clear();
        m_droppedShadowEdgeCount = 0;
    }

    Bool RenderWorld2D::ReserveShadowEdges(std::size_t capacity)
    {
        try
        {
            m_shadowEdges.Reserve(capacity);
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        return true;
    }

    Bool RenderWorld2D::SubmitShadowEdge(const ShadowEdge2DItem& edge)
    {
        if (m_shadowEdges.Size() == m_shadowEdges.Capacity())
        {
            ++m_droppedShadowEdgeCount;
            return false;
        }
        m_shadowEdges.Add(edge);
        return true;
    }

    std::size_t RenderWorld2D::GetShadowEdgeCount() const
    {
        return m_shadowEdges.Size();
    }

    std::size_t RenderWorld2D::GetDroppedShadowEdgeCount() const
    {
        return m_droppedShadowEdgeCount;
    }

    const ShadowEdge2DItem& RenderWorld2D::GetShadowEdge(std::size_t index) const
    {
        return m_shadowEdges[index];
    }

    Bool RenderWorld2D::ReserveLights(std::size_t capacity)
    {
        try
        {
            m_lights.Reserve(capacity);
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        return true;
    }

    Bool RenderWorld2D::SubmitLight(const Light2DRenderItem& item)
    {
        if (m_lights.Size() == m_lights.Capacity())
        {
            ++m_droppedLightCount;
            return false;
        }
        m_lights.Add(item);
        return true;
    }

    std::size_t RenderWorld2D::GetLightCount() const
    {
        return m_lights.Size();
    }

    std::size_t RenderWorld2D::GetDroppedLightCount() const
    {
        return m_droppedLightCount;
    }

    const Light2DRenderItem& RenderWorld2D::GetLight(std::size_t index) const
    {
        return m_lights[index];
    }

    void RenderWorld2D::SetScreenSpace(const ScreenSpaceFrame& frame)
    {
        m_screen = frame;
    }

    const ScreenSpaceFrame& RenderWorld2D::GetScreenSpace() const
    {
        return m_screen;
    }

    std::size_t RenderWorld2D::GetScreenSpriteCount() const
    {
        return m_screenSprites;
    }

    void RenderWorld2D::SetCamera(const RenderCamera2D& camera)
    {
        m_camera = camera;
        m_hasCamera = true;
    }

    void RenderWorld2D::SetUnusableCameraCount(UInt32 count)
    {
        m_unusableCameras = count;
    }

    UInt32 RenderWorld2D::GetUnusableCameraCount() const
    {
        return m_unusableCameras;
    }

    Bool RenderWorld2D::SubmitSprite(const SpriteRenderItem& item)
    {
        if (m_sprites.Size() == m_sprites.Capacity())
        {
            ++m_droppedSpriteCount;
            return false;
        }
        // 순열은 제출과 동시에 자란다. 정렬 전에도 GetSprite 는 제출 순서를 돌려준다.
        SpriteSortKey entry;
        entry.key = MakeSortKey(item);
        entry.index = static_cast<JBro::UInt32>(m_sprites.Size());
        m_sprites.Add(item);
        m_order.Add(entry);
        if (item.screenSpace)
        {
            ++m_screenSprites;
        }
        return true;
    }

    UInt64 RenderWorld2D::MakeSortKey(const SpriteRenderItem& item)
    {
        // 레이어 합성 순서가 가장 바깥이다. 같은 레이어 안에서만 renderOrder 가 의미를 갖는다.
        // 부호 있는 renderOrder 는 최상위 비트만 뒤집어 부호 없는 대소 관계로 옮긴다.
        const UInt64 layer = item.layerOrder;
        const UInt64 order =
            static_cast<JBro::UInt32>(item.renderOrder) ^ 0x80000000u;
        return (layer << 48) | (order << 16);
    }

    void RenderWorld2D::Sort()
    {
        if (m_order.Size() < 2)
        {
            return;
        }
        // 아이템은 제자리에 둔다. 움직이는 것은 16B 키뿐이다.
        const SpriteRenderItem* items = m_sprites.Data();
        std::sort(
            m_order.begin(),
            m_order.end(),
            [items](const SpriteSortKey& left, const SpriteSortKey& right)
        {
            if (left.key != right.key)
            {
                return left.key < right.key;
            }
            // 키가 같을 때만 아이템을 만진다. 먼저 저작한 차례(`drawSequence`, D-296), 그다음 생성 시각 순인 sourceId 로 안정화한다.
            const Int32 leftSequence = items[left.index].drawSequence;
            const Int32 rightSequence = items[right.index].drawSequence;
            if (leftSequence != rightSequence)
            {
                return leftSequence < rightSequence;
            }
            const InstanceId leftSource = items[left.index].sourceId;
            const InstanceId rightSource = items[right.index].sourceId;
            if (leftSource != rightSource)
            {
                return leftSource < rightSource;
            }
            // 한 소스가 여럿을 내면(텍스트의 글자들) 낸 순서다. `std::sort` 는 안정하지 않아, 이것이 없으면 외곽선이 이웃 글자에
            // 겹치는 순서가 프레임마다 달라질 수 있었다(text-plan §3.3 - 정렬 키의 예약 비트 대신 이 자리로 풀었다).
            return left.index < right.index;
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

    const SpriteRenderItem& RenderWorld2D::GetSprite(std::size_t drawIndex) const
    {
        return m_sprites[m_order[drawIndex].index];
    }

    const SpriteRenderItem* RenderWorld2D::GetSubmittedSprites() const
    {
        return m_sprites.Data();
    }
}
