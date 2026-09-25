#pragma once

#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Runtime/ScriptRegistry.h>

#include <type_traits>

namespace JBro
{
    // 공통 생명주기는 Runtime에 두고, 2D 충돌 훅만 이 계층에서 제공한다.
    //
    // **2D 프로젝트의 스크립트는 모두 이 타입에서 파생한다**(D-203). 물리 시스템은 오브젝트에 붙은 스크립트를
    // 이 타입으로 여기고 훅을 부른다 - 프레임 경로에서 dynamic_cast 를 쓰지 않기 위해서다. 그래서 등록은
    // 아래의 `RegisterScriptType2D` 로만 하고, 그것이 파생 관계를 컴파일할 때 검사한다.
    //
    // 가상 함수 표는 스크립트 DLL 과의 ABI 다. 훅을 더하면 `Framework2DServiceContextAbiVersion` 을 올린다.
    class GameScript2D : public GameScriptBase
    {
    public:
        ~GameScript2D() override = default;

        virtual void OnCollisionEnter(const Collision2D& hit);
        virtual void OnCollisionExit(const Collision2D& hit);
        // 트리거는 밀지 않고 알리기만 한다. 넘어오는 point·normal 은 0 이다.
        virtual void OnTriggerEnter(const Collision2D& hit);
        virtual void OnTriggerExit(const Collision2D& hit);
    };

    // 2D 스크립트 모듈이 타입을 호스트에 알리는 유일한 길이다. `GameScriptBase` 에서 바로 파생한 타입은 여기서
    // 컴파일이 실패한다 - 통과시키면 물리가 그 객체를 `GameScript2D` 로 불러 정의되지 않은 동작이 된다.
    template<typename T>
    bool RegisterScriptType2D()
    {
        static_assert(std::is_base_of_v<GameScript2D, T>,
            "a 2D script must derive from JBro::GameScript2D, not GameScriptBase");
        return RegisterScriptType<T>();
    }
}
