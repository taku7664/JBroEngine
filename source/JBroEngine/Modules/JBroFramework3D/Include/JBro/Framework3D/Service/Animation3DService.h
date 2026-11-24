#pragma once

#include <JBro/Framework3D/Handle/Animator3D.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math3D.h>

namespace JBro::Service
{
    // 스크립트가 뼈대 애니메이션에 묻는 값 서비스다(D-460). `GetFramework3DServices().Animation3D` 로 쓴다.
    // 재생 명령은 `Animator3D` 핸들이 하고(D-459), 여기는 자세에서 읽는 것이다. Main-thread only.
    //
    //     const Int32 hand = GetFramework3DServices().Animation3D.FindSocket(animator, "RightHand");   // OnStart 에서 한 번
    //     Vector3 position; Quaternion rotation; Vector3 scale;
    //     if (GetFramework3DServices().Animation3D.GetSocketTransform(animator, hand, position, rotation, scale)) { ... }
    class Animation3DService
    {
    public:
        // 소켓 이름을 번호로 푼다: 뼈대 소켓 → 그 애니메이터를 따르는 메시의 소켓 → 뼈 이름. 이름을 견주므로 프레임마다 부르지 않는다. 없으면 -1.
        Int32 FindSocket(Handle::Animator3D animator, const char* name) const;
        // 그 소켓의 월드 위치·회전·크기다. 애니메이터는 스크립트 뒤에 자세를 셈하므로 **지난 프레임의 자세**다. 없으면 거짓이고 값은 그대로다.
        Bool GetSocketTransform(Handle::Animator3D animator, Int32 socket, Vector3& position, Quaternion& rotation, Vector3& scale) const;
    };
}
