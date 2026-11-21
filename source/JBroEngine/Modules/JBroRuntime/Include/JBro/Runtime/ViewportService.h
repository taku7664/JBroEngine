#pragma once

#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/LayerHandle.h>
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

namespace JBro::Service
{
    // **스크립트가 캔버스의 뷰포트 목록을 바꾸는 길이다**(D-438, viewport-plan §4.6). `GetServiceContext().Viewports` 로 부른다.
    //
    //   const Int32 right = GetServiceContext().Viewports.Find("Player2");
    //   GetServiceContext().Viewports.SetActive(right, true);                 // 분할 화면을 켠다
    //   GetServiceContext().Viewports.SetSize(0, Vector2{0.5f, 1.0f});       // 첫 화면을 왼쪽 반으로
    //   GetServiceContext().Viewports.SetCamera(right, player2Camera);         // 그 화면의 눈
    //
    // 뷰포트는 목록 차례로 그린다. 사각형은 대상 안의 **비율(왼쪽 위 원점, 0..1) x 대상 크기 + 픽셀**이다. 카메라가 비면 **자동**이고, 그 차원의 첫 켜진 카메라다.
    // 번호가 목록 밖이면 아무것도 하지 않고 기본값을 준다. 캔버스가 묶이지 않은 자리에서는 빈 목록이다.
    // 스크립트가 바꾼 값은 캔버스 파일에 적히지 않는다 - 에디터의 재생을 멈추면 돌아온다.
    // 가상 함수가 없다 - 서비스 컨텍스트 안에 값으로 들어간다.
    class ViewportService
    {
    public:
        UInt32 GetCount() const;
        // 이름이 같은 첫 뷰포트의 번호. 없으면 -1 이다. 이름을 견주므로 `OnStart` 에서 찾아 필드에 든다.
        Int32 Find(const char* name) const;
        Bool IsActive(UInt32 index) const;
        void SetActive(UInt32 index, Bool active) const;
        Vector2 GetOrigin(UInt32 index) const;
        void SetOrigin(UInt32 index, Vector2 origin) const;
        Vector2 GetSize(UInt32 index) const;
        void SetSize(UInt32 index, Vector2 size) const;
        // 앞 뷰포트가 그린 대상에서 제 사각형을 카메라의 지우기 색으로 먼저 칠하는가.
        Bool GetClearViewport(UInt32 index) const;
        void SetClearViewport(UInt32 index, Bool clear) const;
        // 정해 둔 카메라 오브젝트. 자동이면 빈 핸들이다.
        Handle::GameObject GetCamera(UInt32 index) const;
        // 그 오브젝트의 카메라로 그린다. 무효한 핸들이면 바꾸지 않고 거짓이다.
        Bool SetCamera(UInt32 index, Handle::GameObject camera) const;
        // 자동(첫 켜진 카메라)으로 되돌린다.
        void SetAutoCamera(UInt32 index) const;
        // 비율 위에 더하는 픽셀이다(D-439). 오른쪽 위 미니맵: `SetOrigin(i, {1, 0})`, `SetOriginPixels(i, {-220, 20})`, `SetSize(i, {0, 0})`, `SetSizePixels(i, {200, 150})`.
        Vector2 GetOriginPixels(UInt32 index) const;
        void SetOriginPixels(UInt32 index, Vector2 pixels) const;
        Vector2 GetSizePixels(UInt32 index) const;
        void SetSizePixels(UInt32 index, Vector2 pixels) const;
        // 화면 대신 그리는 렌더 텍스처(`.jrt`)다. 빈 핸들이면 화면에 그린다. 에디터가 캔버스 에셋을 다시 묶으면(에셋을 바꿀 때) 캔버스 파일의 값으로 돌아간다.
        AssetHandle GetRenderTexture(UInt32 index) const;
        void SetRenderTexture(UInt32 index, AssetHandle texture) const;
        // **그릴 레이어**다. 월드 레이어는 처음에 모두 그리고, 하나를 끄면 나머지만 그린다. 화면 레이어는 켠 것만 이 뷰포트의 사각형 안에 그린다 -
        // 어느 뷰포트도 켜지 않은 화면 레이어는 화면 전체에 한 번 그린다(분할 화면의 플레이어별 HUD 는 각 뷰포트에서 그 HUD 레이어를 켠다).
        Bool DrawsLayer(UInt32 index, Handle::Layer layer) const;
        void SetLayerDrawn(UInt32 index, Handle::Layer layer, Bool drawn) const;
        // 처음으로 - 월드 레이어는 모두, 화면 레이어는 없음.
        void ResetLayers(UInt32 index) const;
    };
}
