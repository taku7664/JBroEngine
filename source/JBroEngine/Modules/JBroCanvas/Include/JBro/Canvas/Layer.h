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

    // 레이어가 그려지는 공간이다(D-237, ui-plan §2.1). `Screen` 은 카메라와 무관한 화면 좌표(기준 해상도의 픽셀)이고 월드 뒤에 그려진다.
    enum class LayerSpace : std::uint8_t
    {
        World,
        Screen,
    };

    // 화면 레이어가 대상 크기에 맞추는 방식이다(`ScreenSpace.h`). 월드 레이어는 보지 않는다.
    enum class ScreenScaleMode : std::uint8_t
    {
        FixedHeight,
        FixedWidth,
        Contain,
        ConstantPixel,
    };

    // 레이어를 아래에 얹는 방식이다(D-279, 기존 `ELayerBlendMode`). `Normal` 이 아니거나 불투명도가 1 보다 작으면 그 레이어는 제 텍스처에
    // 먼저 그려진 뒤 한 장으로 얹힌다 - 레이어 안의 스프라이트끼리는 보통 알파로 겹치고, 블렌드와 불투명도는 레이어 전체에 한 번 걸린다.
    enum class LayerBlend : std::uint8_t
    {
        Normal,
        Additive,
        Multiply,
        Screen,
    };

    // 파일과 인스펙터의 이름이다. 모르는 이름은 거짓이고 결과를 건드리지 않는다.
    const char* LayerBlendName(LayerBlend blend);
    bool ParseLayerBlend(const char* name, LayerBlend& blend);

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

        LayerSpace GetSpace() const;
        void SetSpace(LayerSpace space);
        ScreenScaleMode GetScaleMode() const;
        void SetScaleMode(ScreenScaleMode mode);

        LayerBlend GetBlend() const;
        void SetBlend(LayerBlend blend);
        // 0..1 로 자른다. 유한하지 않은 값은 받지 않는다(그대로 둔다).
        float GetOpacity() const;
        void SetOpacity(float opacity);
        // 제 텍스처에 그려 얹어야 하는가 - 블렌드가 `Normal` 이 아니거나 불투명도가 1 보다 작다.
        bool NeedsComposite() const;

    private:
        friend class Canvas;

        void SetOrder(LayerOrder order);

        LayerId    m_id = InvalidLayerId;
        LayerOrder m_order = 0;
        char       m_name[64]{};
        bool       m_visible = true;
        LayerSpace m_space = LayerSpace::World;
        ScreenScaleMode m_scaleMode = ScreenScaleMode::FixedHeight;
        LayerBlend m_blend = LayerBlend::Normal;
        float      m_opacity = 1.0f;
    };
}
