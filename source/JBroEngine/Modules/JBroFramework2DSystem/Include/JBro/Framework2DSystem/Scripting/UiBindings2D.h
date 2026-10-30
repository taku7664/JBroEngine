#pragma once

#include <JBro/ScreenUi/UiHost.h>

namespace JBro
{
    class Canvas;
}

namespace JBro::System
{
    // **2D 의 화면 UI 연결**(D-362). 훅은 `GameScript2D` 로 부르고(2D 프로젝트의 스크립트는 모두 그 타입이다, D-207), 월드 레이어는 카메라 2D 로 역투영한다
    // (그 픽셀이 든 뷰포트의 카메라, 패럴랙스는 그리기와 같은 양을 뺀다 - D-286·D-359).
    class UiHookDispatcher2D final : public IUiHookDispatcher
    {
    public:
        void Call(GameScriptBase& script, UiHook hook) override;
    };

    class WorldLayerMapper2D final : public IWorldLayerMapper
    {
    public:
        void SetCanvas(Canvas* canvas);
        Bool PixelToLayer(const Layer& layer, const ScreenSpaceFrame& screen, Float pixelX, Float pixelY, Vector2& point) const override;
        Bool LayerToPixel(const Layer& layer, const ScreenSpaceFrame& screen, Vector2 point, Float& pixelX, Float& pixelY) const override;

    private:
        Canvas* m_canvas = nullptr;
    };
}
