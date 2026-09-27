#pragma once

#include <JBro/Canvas/Layer.h>
#include <JBro/Types/SafeArea.h>

#include <cstdint>

// 화면 레이어의 좌표(D-237, ui-plan §2.2). **1 유닛 = 기준 해상도의 1 픽셀**이고 원점은 화면 가운데, y 는 위다.
//
// 보이는 영역을 재는 함수는 이것 하나다 - 앵커(`Transform2DSystem`)·그리기(렌더 브리지)·포인터 역투영이 모두 이것을 부른다.
// 기존 엔진은 앵커와 그리기가 다른 크기를 써서 분할 화면에서 어긋났다(ui-plan §1.2 U2).
namespace JBro
{
    // 한 프레임의 화면 기준이다. 기준은 프로젝트의 `ResolutionWidth/Height`, 대상은 이번 프레임에 게임이 그려지는 크기(픽셀)다.
    struct ScreenSpaceFrame
    {
        float referenceWidth = 1920.0f;
        float referenceHeight = 1080.0f;
        float targetWidth = 0.0f;
        float targetHeight = 0.0f;
        // 게임이 실제로 그려지는 사각형이다(대상 픽셀, 왼쪽 위 원점, D-239). 폭이나 높이가 0 이면 대상 전체다.
        // `PixelPerfect` 카메라의 레터박스가 이것을 줄인다 - 화면 레이어의 크기 재기와 포인터 역투영이 모두 이 사각형을 쓴다.
        float areaX = 0.0f;
        float areaY = 0.0f;
        float areaWidth = 0.0f;
        float areaHeight = 0.0f;
        // 가장자리에서 가려지는 띠다(대상 픽셀, D-249). 노치·홈 표시줄이 먹는 자리이고 데스크톱은 0 이다.
        // **그리는 영역은 이것에 줄어들지 않는다** - 그림은 화면 끝까지 가는 것이 맞다.
        // 줄어드는 것은 `GetSafeScreenArea` 가 답하는 안쪽 영역뿐이고, 사람이 눌러야 하는 것이 그 안에 놓인다.
        SafeAreaInsets safeArea;
    };

    // 대상 안의 사각형이다(대상 픽셀, 왼쪽 위 원점).
    struct ScreenArea
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    // 그려지는 사각형이다. `area*` 가 비었으면 대상 전체다. 대상이 0 이하·무한이면 거짓이고 결과를 건드리지 않는다.
    bool GetScreenArea(const ScreenSpaceFrame& frame, ScreenArea& area);

    // **가려지지 않는 안쪽 사각형이다**(D-249). 그려지는 사각형에서 `safeArea` 만큼 줄인 것이고,
    // 띠가 없으면 `GetScreenArea` 와 같다. 띠가 너무 두꺼워 남는 것이 없으면 폭이나 높이가 0 이 된다 -
    // 음수로 뒤집지 않는다. 글자·버튼처럼 가려지면 안 되는 것을 놓을 때 이것을 쓴다.
    bool GetSafeScreenArea(const ScreenSpaceFrame& frame, ScreenArea& area);

    // **기준 해상도를 정수 배율로 대상 가운데에 놓는 사각형이다**(D-239, `PixelPerfect` 카메라의 레터박스).
    // 배율은 `floor(min(대상 폭 / 기준 폭, 대상 높이 / 기준 높이))` 이고 사각형의 왼쪽 위는 정수 픽셀이다. 대상이 기준보다 작아 배율이
    // 1 보다 작으면 비정수 배율로 줄여 넣는다 - 픽셀은 맞지 않지만 보이기는 한다. `scale` 이 그 배율이다.
    // 기준이나 대상이 0 이하·무한이면 거짓이고 결과를 건드리지 않는다.
    bool ComputePixelPerfectArea(const ScreenSpaceFrame& frame, ScreenArea& area, float& scale);

    // 화면 레이어에서 보이는 반폭·반높이(기준 픽셀)다.
    struct ScreenExtent
    {
        float halfWidth = 0.0f;
        float halfHeight = 0.0f;
    };

    // 맞춤 방식대로 보이는 영역을 잰다. "대상" 은 그려지는 사각형(`GetScreenArea`)이다. 기준이나 대상이 0 이하·무한이면 거짓이다.
    //
    //     FixedHeight    기준 높이를 늘 다 보인다. 폭은 대상의 가로세로비를 따른다
    //     FixedWidth     기준 폭을 늘 다 보인다
    //     Contain        기준 사각형 전체가 들어가게 한다(남는 쪽이 넓어진다)
    //     ConstantPixel  기준과 무관하게 대상의 1 픽셀이 1 유닛이다
    bool ComputeScreenExtent(ScreenScaleMode mode, const ScreenSpaceFrame& frame, ScreenExtent& extent);

    // 앵커(0..1, y 위)가 가리키는 화면의 점이다(기준 픽셀, 가운데 원점).
    void ComputeAnchorPoint(const ScreenExtent& extent, float anchorX, float anchorY, float& x, float& y);

    // **역투영**(3 단계). 게임 화면 픽셀(왼쪽 위 원점, y 아래 - 마우스·터치의 좌표)과 보이는 영역의 -1..1 자리를 오간다.
    // 화면 레이어는 이 자리에 `ScreenExtent` 를 곱하면 기준 픽셀이고, 월드 레이어는 카메라의 반폭·반높이를 곱하면 뷰 좌표다.
    // 대상 크기가 0 이하·무한이면 거짓이고 결과를 건드리지 않는다.
    bool ScreenPixelToNormalized(const ScreenSpaceFrame& frame, float pixelX, float pixelY, float& x, float& y);
    bool NormalizedToScreenPixel(const ScreenSpaceFrame& frame, float x, float y, float& pixelX, float& pixelY);
    // 게임 화면 픽셀 ↔ 화면 레이어의 좌표(기준 픽셀, 가운데 원점, y 위)다. 위 둘과 `ComputeScreenExtent` 를 잇는다.
    bool ScreenPixelToLayer(ScreenScaleMode mode, const ScreenSpaceFrame& frame, float pixelX, float pixelY, float& x, float& y);
    bool LayerToScreenPixel(ScreenScaleMode mode, const ScreenSpaceFrame& frame, float x, float y, float& pixelX, float& pixelY);

    // 파일과 인스펙터의 이름이다. 모르는 이름은 거짓이고 결과를 건드리지 않는다.
    const char* LayerSpaceName(LayerSpace space);
    bool ParseLayerSpace(const char* name, LayerSpace& space);
    const char* ScreenScaleModeName(ScreenScaleMode mode);
    bool ParseScreenScaleMode(const char* name, ScreenScaleMode& mode);
}
