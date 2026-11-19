#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/UInt.h>

namespace JBro::System
{
    // 스크립트가 카메라의 후처리 필드를 덮어쓰는 길이다(D-346). 캔버스를 가진 프레임워크가 구현(호스트의 `PostProcessSystem`)을 들고,
    // 스크립트 컨텍스트를 묶을 때 공통 시스템 컨텍스트(`SystemContext::PostProcess`)에 꽂는다. 가상 함수 표는 스크립트 DLL 과의 ABI 다 -
    // 바꾸면 `SystemContextAbiVersion` 을 올린다.
    //
    // 필드는 효과 이름과 필드 이름으로 찾은 번호로 가리킨다(`InvalidField` 는 없다). 덮어쓴 값은 카메라의 프로필과 볼륨을 섞은 맨 끝에 얹히고,
    // 지울 때까지 프레임을 넘어 남는다. 메인 스레드 전용이다.
    class IPostProcessSystem
    {
    public:
        virtual ~IPostProcessSystem() = default;

        virtual UInt32 FindField(const char* effect, const char* field) const = 0;
        // 필드의 타입과 맞아야 한다(`Float` 필드에 `SetFloat`). 맞지 않거나 자리가 차면 거짓이다.
        virtual Bool SetFloat(UInt32 field, Float value) = 0;
        virtual Bool SetInt(UInt32 field, Int32 value) = 0;
        virtual Bool SetBool(UInt32 field, Bool value) = 0;
        virtual Bool SetColor(UInt32 field, Color value) = 0;
        virtual Bool SetVector2(UInt32 field, Vector2 value) = 0;
        virtual void Clear(UInt32 field) = 0;
        virtual void ClearAll() = 0;

        static constexpr UInt32 InvalidField = 0xFFFFFFFFu;
    };
}
