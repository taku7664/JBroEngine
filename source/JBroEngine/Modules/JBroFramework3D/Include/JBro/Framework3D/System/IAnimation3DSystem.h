#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math3D.h>

namespace JBro::Component
{
    class Animator3D;
    class BoneAttachment3D;
}

namespace JBro::System
{
    // 스크립트 서비스가 호스트의 애니메이터 시스템에 묻는 길이다(D-460, `IPhysics3DSystem` 과 같은 자리). 가상 함수 표가 스크립트 DLL 과의 ABI 이므로
    // 바꾸면 `Framework3DSystemContextAbiVersion` 을 올린다. Main-thread only.
    class IAnimation3DSystem
    {
    public:
        virtual ~IAnimation3DSystem() = default;

        // 소켓(또는 뼈) 이름을 그 애니메이터의 소켓 번호로 푼다. 이름을 견주므로 시작할 때 한 번 부르고 번호를 들고 있는다. 없으면 -1.
        virtual Int32 FindSocket(const Component::Animator3D& animator, const char* name) = 0;
        // 그 번호 소켓의 월드 자리다(마지막으로 셈한 자세). 번호가 없거나 자세가 아직 서지 않았으면 거짓이고 값은 그대로다.
        // 뼈대·소켓이 다시 읽히면 같은 이름으로 다시 푼다.
        virtual Bool GetSocketTransform(const Component::Animator3D& animator, Int32 socket, Vector3& position, Quaternion& rotation, Vector3& scale) = 0;
    };
}
