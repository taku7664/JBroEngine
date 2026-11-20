#pragma once

#include <JBro/Graphics/Renderer.h>
#include <JBro/ScreenUi/Rendering/RenderWorld2D.h>
#include <JBro/ScreenUi/Rendering/SpritePieces.h>

#include <cstddef>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// **화면 UI 의 스프라이트 제출**(D-362). 2D 렌더 브리지에서 뗀 조각이다 - 렌더 월드의 스프라이트를 열린 뷰에 밀어 넣고(`PushSprites`), 화면 레이어를
// 맞춤 방식마다 뷰 하나로 낸다(`SubmitScreenViews`). 2D 브리지가 월드 뷰에, 3D 브리지가 제 뷰 위의 화면 레이어에 쓴다.
namespace JBro::Internal
{
    // 모아 둔 스프라이트를 이미 열린 뷰에 밀어 넣는다. 시뮬레이션 뷰와 캔버스 뷰가
    // 같은 목록을 쓰므로 이 부분만 따로 뗀다.
    //
    // `editorView` 면 **에디터에서 감춘 오브젝트를 건너뛴다**(D-163, 기존 `EditorHidden`). 시뮬레이션 뷰는 보지 않는다 -
    // 감추는 것은 편집을 위한 것이지 게임의 모습이 아니다.
    // 어느 아이템을 그 뷰에 넣는가. 월드 뷰는 월드 레이어만, 화면 뷰는 그 맞춤 방식의 화면 레이어만이다(D-237).
    struct SpriteFilterRule
    {
        Bool screenSpace = false;
        ScreenScaleMode scaleMode = ScreenScaleMode::FixedHeight;
        Bool anyScaleMode = true;
        // 있으면 이 오브젝트와 그 자손의 아이템만 넣는다(D-252). 캔버스 뷰가 흰 막 위에 들어간 오브젝트를 다시 그릴 때다.
        InstanceId focus = InvalidInstanceId;
        // 있으면 이 오브젝트들의 아이템만 넣는다(D-276). 선택 외곽선의 마스크를 그릴 때다. 자손은 따로 고른 것만이다 -
        // 캔버스 뷰의 고르기가 자손까지 담는다(D-253·D-254).
        const InstanceId* selection = nullptr;
        UInt32 selectionCount = 0;
        // 레이어의 블렌드와 불투명도를 렌더러의 묶음으로 낸다(D-279). 선택 외곽선의 마스크는 그린 그대로의 모양이어야 하므로 끈다.
        Bool composite = true;
        // 레이어의 프로필 효과(D-345)를 묶음에 건다. 캔버스 뷰의 후처리 단추를 끄면 레이어 효과도 꺼진다. `composite` 가 거짓이면 보지 않는다.
        Bool layerPostProcess = true;
        // 있으면 월드 레이어의 패럴랙스를 이 뷰(월드 → 뷰)로 건다(D-286). 게임 화면만 준다 - 캔버스 뷰는 패럴랙스 없이 배치하는 자리다.
        const Matrix3x2* parallaxView = nullptr;
        // 빛을 받는 레이어의 아이템을 렌더러의 빛을 받는 구간으로 낸다(D-291). 라이트를 낸 월드 뷰만 켠다 - 선택 외곽선의 마스크·레이어 썸네일·
        // 화면 레이어는 빛을 보지 않는다.
        Bool lighting = false;
        // 0 이상이면 이 레이어 차례의 아이템만 넣는다(썸네일, D-288).
        Int32 layerOrder = -1;
        // 이 텍스처를 쓰는 아이템은 넣지 않는다(D-362). 렌더 텍스처 카메라의 뷰는 제 대상을 읽을 수 없다 - 그 텍스처를 보이는 스프라이트를 뺀다.
        AssetHandle excludeTexture;
        // 있으면 이 마스크가 담은 레이어 차례의 아이템만 넣는다(D-438, 뷰포트의 레이어 필터). 게임 화면의 뷰만 준다.
        const ViewLayerMask* layers = nullptr;
    };

    SpriteSubmit BuildSprite(const SpriteRenderItem& source, Float offsetX, Float offsetY, const SpritePiece& piece);
    Bool PushSprites(const RenderWorld2D& world, Renderer& renderer, Bool editorView, const SpriteFilterRule& rule,
        std::size_t first = 0, std::size_t last = static_cast<std::size_t>(-1));
    // 화면 레이어의 정사영이다(D-237). 가운데 원점, y 위, 기준 픽셀 - 앵커와 같은 `ComputeScreenExtent` 로 잰다.
    Bool BuildScreenCamera(const ScreenExtent& extent, const ScreenArea& area, CameraParams& result);
    // 그리는 순서의 화면 아이템을 맞춤 방식이 같은 것끼리 이어진 덩어리로 나눠 덩어리마다 뷰 하나에 그린다. 화면 아이템이 없으면 아무것도 내지 않는다.
    Bool SubmitScreenViews(const RenderWorld2D& world, Renderer& renderer, const ScreenSpaceFrame& frame, Bool& submitted);
}
