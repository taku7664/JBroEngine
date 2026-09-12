#pragma once

#include <cstdint>

namespace JBro
{
    // 레이어의 영속 식별자다. 단조 증가하며 재사용하지 않고, 직렬화되는 값이다(D-46).
    // 합성 순서와는 다른 개념이다 — 순서는 Canvas 안의 위치이고 이동으로 바뀌지만 이 값은 불변이다.
    using LayerId = std::uint32_t;
    inline constexpr LayerId InvalidLayerId = static_cast<LayerId>(-1);

    // 합성 순서. 렌더 정렬 키의 최상위 필드라 매 오브젝트가 읽는다.
    using LayerOrder = std::uint16_t;

    class Layer final
    {
    public:
        Layer(LayerId id, const char* name);

        LayerId     GetId()   const;
        const char* GetName() const;
        void        SetName(const char* name);

        // Canvas 안의 아래→위 순서. 캔버스가 레이어 생성·파괴·이동에서 재색인하며
        // 그 외에는 쓰지 않는다. 매 프레임 렌더 수집이 오브젝트마다 읽으므로 캐시해 둔다.
        LayerOrder GetOrder() const;

        // false 는 렌더만 끈다. 시뮬레이션은 계속한다.
        bool IsVisible() const;
        void SetVisible(bool visible);

    private:
        friend class Canvas;

        void SetOrder(LayerOrder order);

        LayerId    m_id = InvalidLayerId;
        LayerOrder m_order = 0;
        char       m_name[64]{};
        bool       m_visible = true;
    };
}
