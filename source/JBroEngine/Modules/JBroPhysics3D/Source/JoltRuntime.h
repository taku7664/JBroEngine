#pragma once

#include <JBro/Types/Bool.h>

namespace JBro::Physics3D::Internal
{
    // Jolt 의 프로세스 전역 준비다: 할당 훅, 로그, 타입 팩토리와 등록. 월드가 하나라도 살아 있는 동안 서 있고, 마지막 월드가
    // 내려가면 같이 내려간다. 메인 스레드에서만 만들고 지운다(World 와 같다).
    class JoltRuntime
    {
    public:
        JoltRuntime();
        ~JoltRuntime();
        JoltRuntime(const JoltRuntime&) = delete;
        JoltRuntime& operator=(const JoltRuntime&) = delete;
    };
}
