#pragma once

#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // 런타임 공개 헤더는 에셋 타입 모듈을 include 하지 않는다 - 런타임을 보는 모든 모듈이 그 경로를 받아야 하게 된다(D-439).
    // 핸들을 쓰는 쪽(스크립트·캔버스)은 `<JBro/AssetTypes/AssetTypes.h>` 를 이미 본다.
    struct AssetHandle;
}

namespace JBro::System
{
    // 스크립트의 뷰포트 서비스가 캔버스의 뷰포트 목록에 닿는 길이다(D-438, viewport-plan §4.6). 캔버스를 가진 프레임워크가 구현
    // (`CanvasViewportSystem`)을 들고 스크립트 컨텍스트를 묶을 때 `SystemContext::Viewports` 에 꽂는다. 가상 함수 표는 스크립트 DLL 과의
    // ABI 다 - 바꾸면 `SystemContextAbiVersion` 을 올린다. 번호가 목록 밖이면 아무것도 하지 않고 기본값을 준다. 메인 스레드 전용이다.
    class IViewportSystem
    {
    public:
        virtual ~IViewportSystem() = default;

        virtual UInt32 GetCount() = 0;
        // 이름이 같은 첫 뷰포트의 번호. 없으면 -1 이다.
        virtual Int32 Find(const char* name) = 0;
        virtual Bool IsActive(UInt32 index) = 0;
        virtual void SetActive(UInt32 index, Bool active) = 0;
        virtual Vector2 GetOrigin(UInt32 index) = 0;
        virtual void SetOrigin(UInt32 index, Vector2 origin) = 0;
        virtual Vector2 GetSize(UInt32 index) = 0;
        virtual void SetSize(UInt32 index, Vector2 size) = 0;
        virtual Bool GetClearViewport(UInt32 index) = 0;
        virtual void SetClearViewport(UInt32 index, Bool clear) = 0;
        // 정해 둔 카메라 오브젝트. 자동이면 빈 핸들이다.
        virtual Handle::GameObject GetCamera(UInt32 index) = 0;
        // 무효한 핸들이면 바꾸지 않고 거짓이다. 자동으로 되돌리는 것은 `SetAutoCamera` 다.
        virtual Bool SetCamera(UInt32 index, const Handle::GameObject& camera) = 0;
        virtual void SetAutoCamera(UInt32 index) = 0;
        // 비율 위에 더하는 픽셀(D-439).
        virtual Vector2 GetOriginPixels(UInt32 index) = 0;
        virtual void SetOriginPixels(UInt32 index, Vector2 pixels) = 0;
        virtual Vector2 GetSizePixels(UInt32 index) = 0;
        virtual void SetSizePixels(UInt32 index, Vector2 pixels) = 0;
        // 그리는 렌더 텍스처. 빈 핸들이면 화면이다.
        virtual AssetHandle GetRenderTexture(UInt32 index) = 0;
        virtual void SetRenderTexture(UInt32 index, AssetHandle texture) = 0;
        // 레이어는 캔버스의 영속 번호다(`IObjectSystem` 과 같다).
        virtual Bool DrawsLayer(UInt32 index, UInt32 layer) = 0;
        virtual void SetLayerDrawn(UInt32 index, UInt32 layer, Bool drawn) = 0;
        virtual void ResetLayers(UInt32 index) = 0;
    };
}
