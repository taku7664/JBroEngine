#pragma once

#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/UInt.h>

namespace JBro::Service
{
    // **스크립트가 캔버스의 뷰포트 목록을 바꾸는 길이다**(D-438, viewport-plan §4.6). `GetServiceContext().Viewports` 로 부른다.
    //
    //   const Int32 right = GetServiceContext().Viewports.Find("Player2");
    //   GetServiceContext().Viewports.SetActive(right, true);                 // 분할 화면을 켠다
    //   GetServiceContext().Viewports.SetSize(0, Vector2{0.5f, 1.0f});       // 첫 화면을 왼쪽 반으로
    //   GetServiceContext().Viewports.SetCamera(right, player2Camera);         // 그 화면의 눈
    //
    // 뷰포트는 목록 차례로 그린다. 사각형은 대상 안의 비율(왼쪽 위 원점, 0..1)이다. 카메라가 비면 **자동**이고, 그 차원의 첫 켜진 카메라다.
    // 번호가 목록 밖이면 아무것도 하지 않고 기본값을 준다. 캔버스가 묶이지 않은 자리에서는 빈 목록이다. 레이어 필터와 렌더 텍스처는 에디터에서만 정한다.
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
    };
}
