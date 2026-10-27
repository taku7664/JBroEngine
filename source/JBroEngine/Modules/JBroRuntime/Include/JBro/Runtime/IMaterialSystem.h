#pragma once

#include <JBro/Types/Uuid.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::System
{
    // 스크립트가 머티리얼 에셋의 파라미터를 덮어쓰는 길이다(D-348). 캔버스를 가진 프레임워크가 구현(호스트의 `MaterialSystem`)을 들고,
    // 스크립트 컨텍스트를 묶을 때 공통 시스템 컨텍스트(`SystemContext::Material`)에 꽂는다. 가상 함수 표는 스크립트 DLL 과의 ABI 다 -
    // 바꾸면 `SystemContextAbiVersion` 을 올린다.
    //
    // 파라미터는 머티리얼 아이디와 셰이더의 변수 이름으로 찾은 번호로 가리킨다. 덮어쓴 값은 머티리얼 파일의 값 위에 얹혀 그 머티리얼을 쓰는 모든
    // 스프라이트·메시·효과에 걸리고, 지울 때까지 남는다. 값의 칸 수가 셰이더의 형과 맞지 않으면 얹지 않는다. 메인 스레드 전용이다.
    class IMaterialSystem
    {
    public:
        virtual ~IMaterialSystem() = default;

        virtual UInt32 FindParameter(Uuid material, const char* parameter) = 0;
        // `values` 의 `count`(1..4) 칸을 넣는다. `integer` 면 `int`·`uint`·`bool` 파라미터의 값이다.
        virtual Bool SetValues(UInt32 parameter, const Float* values, UInt32 count, Bool integer) = 0;
        virtual void Clear(UInt32 parameter) = 0;
        virtual void ClearAll() = 0;

        static constexpr UInt32 InvalidParameter = 0xFFFFFFFFu;
    };
}
