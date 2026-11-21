#pragma once

#include <JBro/Runtime/IViewportSystem.h>

namespace JBro
{
    class Canvas;

    // 스크립트의 뷰포트 서비스가 캔버스의 뷰포트 목록에 닿는 구현이다(D-438). 캔버스를 가진 프레임워크가 하나 들고 스크립트 컨텍스트를 묶을 때
    // `SystemContext::Viewports` 에 꽂는다. 차원은 모른다 - 카메라는 오브젝트로만 가리킨다.
    class CanvasViewportSystem final : public System::IViewportSystem
    {
    public:
        CanvasViewportSystem() = default;
        CanvasViewportSystem(const CanvasViewportSystem&) = delete;
        CanvasViewportSystem& operator=(const CanvasViewportSystem&) = delete;

        // `canvas` 가 null 이면 빈 목록이다.
        void Bind(Canvas* canvas);

        UInt32 GetCount() override;
        Int32 Find(const char* name) override;
        Bool IsActive(UInt32 index) override;
        void SetActive(UInt32 index, Bool active) override;
        Vector2 GetOrigin(UInt32 index) override;
        void SetOrigin(UInt32 index, Vector2 origin) override;
        Vector2 GetSize(UInt32 index) override;
        void SetSize(UInt32 index, Vector2 size) override;
        Bool GetClearViewport(UInt32 index) override;
        void SetClearViewport(UInt32 index, Bool clear) override;
        Handle::GameObject GetCamera(UInt32 index) override;
        Bool SetCamera(UInt32 index, const Handle::GameObject& camera) override;
        void SetAutoCamera(UInt32 index) override;
        Vector2 GetOriginPixels(UInt32 index) override;
        void SetOriginPixels(UInt32 index, Vector2 pixels) override;
        Vector2 GetSizePixels(UInt32 index) override;
        void SetSizePixels(UInt32 index, Vector2 pixels) override;
        AssetHandle GetRenderTexture(UInt32 index) override;
        void SetRenderTexture(UInt32 index, AssetHandle texture) override;
        Bool DrawsLayer(UInt32 index, UInt32 layer) override;
        void SetLayerDrawn(UInt32 index, UInt32 layer, Bool drawn) override;
        void ResetLayers(UInt32 index) override;

    private:
        Canvas* m_canvas = nullptr;
    };
}
