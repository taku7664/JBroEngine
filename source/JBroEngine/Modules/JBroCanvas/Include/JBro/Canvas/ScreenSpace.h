#pragma once

#include <JBro/Canvas/Layer.h>

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
    };

    // 화면 레이어에서 보이는 반폭·반높이(기준 픽셀)다.
    struct ScreenExtent
    {
        float halfWidth = 0.0f;
        float halfHeight = 0.0f;
    };

    // 맞춤 방식대로 보이는 영역을 잰다. 기준이나 대상이 0 이하·무한이면 거짓이다.
    //
    //     FixedHeight    기준 높이를 늘 다 보인다. 폭은 대상의 가로세로비를 따른다
    //     FixedWidth     기준 폭을 늘 다 보인다
    //     Contain        기준 사각형 전체가 들어가게 한다(남는 쪽이 넓어진다)
    //     ConstantPixel  기준과 무관하게 대상의 1 픽셀이 1 유닛이다
    bool ComputeScreenExtent(ScreenScaleMode mode, const ScreenSpaceFrame& frame, ScreenExtent& extent);

    // 앵커(0..1, y 위)가 가리키는 화면의 점이다(기준 픽셀, 가운데 원점).
    void ComputeAnchorPoint(const ScreenExtent& extent, float anchorX, float anchorY, float& x, float& y);

    // 파일과 인스펙터의 이름이다. 모르는 이름은 거짓이고 결과를 건드리지 않는다.
    const char* LayerSpaceName(LayerSpace space);
    bool ParseLayerSpace(const char* name, LayerSpace& space);
    const char* ScreenScaleModeName(ScreenScaleMode mode);
    bool ParseScreenScaleMode(const char* name, ScreenScaleMode& mode);
}
