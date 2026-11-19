#pragma once

#include <JBro/Framework3D/ComponentTypes3D.h>
#include <JBro/Types/Array.h>

#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro::System
{
    // 스크립트 서비스가 호스트의 3D 물리에 묻는 길이다(D-417, 2D 의 `IPhysics2DSystem` 자리). 가상 함수 표가 스크립트 DLL 과의 ABI 이므로
    // 바꾸면 `Framework3DSystemContextAbiVersion` 을 올린다.
    //
    // Main-thread only. 질의는 **지금의** 컴포넌트를 본다(D-422, 2D 와 같다) - 이번 프레임에 만든 콜라이더와 스크립트가 옮긴 오브젝트가 바로 걸린다.
    // `direction` 은 정규화하지 않아도 되고 0 이면 맞지 않는다. `layerMask` 와 콜라이더의 `layer` 가 겹치는 것만 대상이고 트리거도 든다.
    // 결과 배열은 비운 뒤 채우며 부르는 쪽의 용량은 남긴다. 시스템이 없으면 결과를 비우고 거짓이다.
    class IPhysics3DSystem
    {
    public:
        virtual ~IPhysics3DSystem() = default;

        // 가장 가까운 콜라이더.
        virtual Bool Raycast(Vector3 origin, Vector3 direction, Float distance, RaycastHit3D& hit, UInt32 layerMask) const = 0;
        // 경로에 걸리는 바디 전부를 거리 순으로. 한 오브젝트의 고체 콜라이더는 한 바디라 한 번이고, 트리거는 저마다다.
        virtual void RaycastAll(Vector3 origin, Vector3 direction, Float distance, Array<RaycastHit3D>& hits, UInt32 layerMask) const = 0;
        // 구·돌린 상자와 겹치는 오브젝트. 오브젝트마다 한 번이다.
        virtual void OverlapSphere(Vector3 center, Float radius, Array<Handle::GameObject>& results, UInt32 layerMask) const = 0;
        virtual void OverlapBox(Vector3 center, Vector3 halfExtents, Quaternion rotation, Array<Handle::GameObject>& results,
            UInt32 layerMask) const = 0;
        // 구·돌린 상자를 밀어 처음 닿는 콜라이더. point 는 맞은 순간의 접촉점이다.
        virtual Bool SphereCast(Vector3 origin, Float radius, Vector3 direction, Float distance, RaycastHit3D& hit,
            UInt32 layerMask) const = 0;
        virtual Bool BoxCast(Vector3 center, Vector3 halfExtents, Quaternion rotation, Vector3 direction, Float distance,
            RaycastHit3D& hit, UInt32 layerMask) const = 0;
    };
}
