#pragma once

#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Types/Array.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro::System
{
    // 스크립트 서비스가 호스트의 물리에 묻는 길이다. 가상 함수 표가 스크립트 DLL 과의 ABI 이므로 바꾸면
    // `Framework2DSystemContextAbiVersion` 을 올린다(D-28).
    //
    // Main-thread only. 모든 질의는 스텝과 무관하게 **지금의** 컴포넌트를 본다. `direction` 은 정규화하지 않아도 되고
    // 0 이면 맞지 않는다. `layerMask` 와 콜라이더의 `layer` 가 겹치는 것만 대상이다. 결과 배열은 비운 뒤 채우며, 부르는 쪽의
    // 용량은 남긴다. 시스템이 없거나 꺼졌으면 결과를 비우고 거짓·빈 핸들이다.
    class IPhysics2DSystem
    {
    public:
        virtual ~IPhysics2DSystem() = default;

        // 가장 가까운 콜라이더.
        virtual Bool Raycast(Vector2 origin, Vector2 direction, Float distance, RaycastHit2D& hit,
            UInt32 layerMask) const = 0;
        // 경로에 걸리는 콜라이더 **전부**를 거리 순으로. 한 오브젝트의 콜라이더 여럿은 각각 따로 든다(관통 판정).
        virtual void RaycastAll(Vector2 origin, Vector2 direction, Float distance, Array<RaycastHit2D>& hits,
            UInt32 layerMask) const = 0;
        // 축 정렬 상자와 겹치는 오브젝트. 오브젝트마다 한 번이다.
        virtual void OverlapBox(const Rect& area, Array<GameObjectHandle>& results,
            UInt32 layerMask) const = 0;
        // 점을 품은 첫 오브젝트. 없으면 빈 핸들이다.
        virtual GameObjectHandle OverlapPoint(Vector2 point, UInt32 layerMask) const = 0;
        // 원과 겹치는 오브젝트. 오브젝트마다 한 번이다.
        virtual void OverlapCircle(Vector2 center, Float radius, Array<GameObjectHandle>& results,
            UInt32 layerMask) const = 0;
        // 원을 밀어 처음 닿는 콜라이더. point 는 맞은 순간의 접촉점이다.
        virtual Bool CircleCast(Vector2 origin, Float radius, Vector2 direction, Float distance, RaycastHit2D& hit,
            UInt32 layerMask) const = 0;
        // 돌린 상자(중심·절반 크기·라디안, 반시계 양수)를 밀어 처음 닿는 콜라이더. 도형은 돌지 않는다.
        virtual Bool BoxCast(Vector2 center, Vector2 halfExtents, Float angle, Vector2 direction, Float distance,
            RaycastHit2D& hit, UInt32 layerMask) const = 0;
    };
}
