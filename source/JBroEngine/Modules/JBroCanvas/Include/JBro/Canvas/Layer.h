#pragma once

#include <JBro/Types/Uuid.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // 레이어의 영속 식별자다. 단조 증가하며 재사용하지 않고, 직렬화되는 값이다(D-46).
    // 합성 순서와는 다른 개념이다 — 순서는 Canvas 안의 위치이고 이동으로 바뀌지만 이 값은 불변이다.
    using LayerId = UInt32;
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
    // 앞의 넷은 하드웨어 블렌드로 얹고, `Subtract` 부터는 아래 그림을 복사해 셰이더가 포토샵의 식으로 섞는다(D-283). 값의 차례는 파일과
    // 렌더러의 `CompositeBlend` 가 함께 쓰므로 뒤에만 더한다.
    enum class LayerBlend : std::uint8_t
    {
        Normal,
        Additive,
        Multiply,
        Screen,
        Subtract,
        Lighten,
        Darken,
        Overlay,
        SoftLight,
        HardLight,
        ColorDodge,
        ColorBurn,
        Difference,
    };
    inline constexpr UInt32 LayerBlendCount = 13;

    // 파일과 인스펙터의 이름이다. 모르는 이름은 거짓이고 결과를 건드리지 않는다.
    const char* LayerBlendName(LayerBlend blend);
    Bool ParseLayerBlend(const char* name, LayerBlend& blend);

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
        Bool IsVisible() const;
        void SetVisible(Bool visible);

        LayerSpace GetSpace() const;
        void SetSpace(LayerSpace space);
        ScreenScaleMode GetScaleMode() const;
        void SetScaleMode(ScreenScaleMode mode);

        LayerBlend GetBlend() const;
        void SetBlend(LayerBlend blend);
        // 0..1 로 자른다. 유한하지 않은 값은 받지 않는다(그대로 둔다).
        Float GetOpacity() const;
        void SetOpacity(Float opacity);
        // 제 텍스처에 그려 얹어야 하는가 - 블렌드가 `Normal` 이 아니거나 불투명도가 1 보다 작다.
        Bool NeedsComposite() const;

        // **패럴랙스 계수**(D-286, 기존 `ParallaxFactor`). 이 레이어를 그리는 카메라의 위치만 이 배가 된다 - 1 은 카메라와 같이, 0.5 는 절반 빠르기의
        // 원경, 0 은 월드 원점에 붙는다. 회전·줌은 그대로라 0 도 화면 고정이 아니다(화면 고정은 화면 레이어다). 화면 레이어는 보지 않는다.
        // 게임 화면에만 걸리고 캔버스 뷰는 보지 않는다. 0 보다 작거나 유한하지 않은 값은 받지 않는다.
        Float GetParallax() const;
        void SetParallax(Float factor);

        // **이 레이어가 어느 레이어 에셋(`.jlayer`)에서 왔는가**(D-287, 기존 `SourceAssetGuid`). 정체 표시일 뿐이다 - 내용은 캔버스에 따로 살고, 레이어를
        // 고쳐도 그 파일은 그대로다. 비어 있으면 캔버스 안에서만 사는 레이어다. 캔버스 파일의 `SourceAsset` 으로 적는다.
        const Uuid& GetSourceAsset() const;
        void SetSourceAsset(const Uuid& asset);

    private:
        friend class Canvas;

        void SetOrder(LayerOrder order);

        LayerId    m_id = InvalidLayerId;
        LayerOrder m_order = 0;
        char       m_name[64]{};
        Bool       m_visible = true;
        LayerSpace m_space = LayerSpace::World;
        ScreenScaleMode m_scaleMode = ScreenScaleMode::FixedHeight;
        LayerBlend m_blend = LayerBlend::Normal;
        Float      m_opacity = 1.0f;
        Float      m_parallax = 1.0f;
        Uuid       m_sourceAsset;
    };
}
