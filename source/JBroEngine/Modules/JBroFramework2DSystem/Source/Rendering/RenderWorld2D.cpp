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
        m_sprites.Clear();
        m_order.Clear();
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
        // 순열은 제출과 동시에 자란다. 정렬 전에도 GetSprite 는 제출 순서를 돌려준다.
        SpriteSortKey entry;
        entry.key = MakeSortKey(item);
        entry.index = static_cast<std::uint32_t>(m_sprites.Size());
        m_sprites.Add(item);
        m_order.Add(entry);
        return true;
    }

    std::uint64_t RenderWorld2D::MakeSortKey(const SpriteRenderItem& item)
    {
        // 레이어 합성 순서가 가장 바깥이다. 같은 레이어 안에서만 renderOrder 가 의미를 갖는다.
        // 부호 있는 renderOrder 는 최상위 비트만 뒤집어 부호 없는 대소 관계로 옮긴다.
        const std::uint64_t layer = item.layerOrder;
        const std::uint64_t order =
            static_cast<std::uint32_t>(item.renderOrder) ^ 0x80000000u;
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
            // 키가 같을 때만 아이템을 만진다. 생성 시각 순인 sourceId 로 안정화한다.
            return items[left.index].sourceId < items[right.index].sourceId;
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
