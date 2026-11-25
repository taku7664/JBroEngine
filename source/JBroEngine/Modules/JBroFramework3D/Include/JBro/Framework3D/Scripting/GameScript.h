#pragma once

#include <JBro/Framework3D/ComponentTypes3D.h>
#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Runtime/ScriptRegistry.h>

#include <type_traits>
#include <JBro/Types/Float.h>

namespace JBro
{
    // **3D 프로젝트의 스크립트는 모두 이 타입에서 파생한다**(D-363). 공통 생명주기는 Runtime 의 `GameScriptBase` 에 있고, 여기에는 3D 캔버스의
    // 화면 레이어 UI 가 부르는 훅이 있다 - 화면 UI 시스템은 오브젝트에 붙은 스크립트를 이 타입으로 여기고 훅을 부른다(프레임 경로에서 dynamic_cast 를
    // 쓰지 않는다). 그래서 등록은 `RegisterScriptType3D` 로만 하고, 그것이 파생 관계를 컴파일할 때 검사한다.
    //
    // 가상 함수 표는 스크립트 DLL 과의 ABI 다. 훅을 더하면 `Framework3DServiceContextAbiVersion` 을 올린다. 충돌 훅은 D-417 에서 더했다.
    class GameScript3D : public GameScriptBase
    {
    public:
        ~GameScript3D() override = default;

        // **같은 오브젝트의 `Button2D` 가 부른다**(D-237·D-363). 3D 캔버스의 화면 레이어에 둔 위젯이고, 2D 의 `GameScript2D` 와 같은 때에 불린다 -
        // 입력 체인 안에서(고정 스텝·`OnUpdate` 보다 먼저). 들어옴·나감은 포인터가 그 버튼의 가장 위에 오고 갈 때, 누름·뗌은 그 버튼에서 누른 것에만,
        // `OnClick` 은 버튼 위에서 누르고 뗐을 때다.
        virtual void OnPointerEnter();
        virtual void OnPointerExit();
        virtual void OnPointerDown();
        virtual void OnPointerUp();
        virtual void OnClick();
        // 같은 오브젝트의 `Slider2D`·`Toggle2D` 가 포인터로 값을 바꾼 프레임에.
        virtual void OnValueChanged();
        // 같은 오브젝트의 `InputField2D` 에서 Enter 를 누른 프레임에.
        virtual void OnSubmit();
        // 같은 오브젝트의 `Draggable2D` 가 끌기를 시작하고 놓은 프레임에. `OnDrop` 은 놓인 자리의 `DropTarget2D` 가 제 오브젝트의 스크립트에 부른다.
        virtual void OnDragBegin();
        virtual void OnDragEnd();
        virtual void OnDrop();
        // 같은 오브젝트의 `ListView2D` 가 항목을 묶을 때와 `variableSize` 목록이 항목을 잴 때.
        virtual void OnListItem();
        virtual void OnListMeasure();

        // **3D 물리가 부른다**(D-417, 2D 의 `GameScript2D` 와 같은 이름·같은 때). 고정 스텝이 끝난 뒤, 이 오브젝트의 콜라이더가 다른 오브젝트의
        // 콜라이더와 닿기 시작했을 때·계속 닿아 있을 때(잠들면 멈춘다)·떨어졌을 때다. 둘 중 하나가 트리거면 `OnTrigger*` 다.
        // `hit.normal` 은 이 오브젝트에서 상대 쪽으로 간다. 끝 훅의 점과 법선은 0 이다.
        virtual void OnCollisionEnter(const Collision3D& hit);
        virtual void OnCollisionStay(const Collision3D& hit);
        virtual void OnCollisionExit(const Collision3D& hit);
        virtual void OnTriggerEnter(const Collision3D& hit);
        virtual void OnTriggerStay(const Collision3D& hit);
        virtual void OnTriggerExit(const Collision3D& hit);
        // **이 오브젝트의 조인트가 끊겼다**(D-437, 2D 와 같은 이름·같은 때). 조인트 컴포넌트는 이미 꺼져 있다. 끊긴 조인트마다 한 번이다.
        virtual void OnJointBreak(Float force, Float torque);
        // **이 오브젝트의 애니메이터가 클립의 이벤트를 지났다**(D-462). 자세를 셈한 뒤(스크립트의 `OnUpdate` 뒤, 렌더 앞) 지난 차례대로 불린다.
        // 덮어쓰는 레이어는 가중치가 0 보다 클 때만이고, 크로스페이드로 빠져나가는 클립의 이벤트는 불리지 않는다. 시간을 끌어 옮기면(에디터) 불리지 않는다.
        virtual void OnAnimationEvent(const AnimationEvent3D& event);
    };

    // 3D 스크립트 모듈이 타입을 호스트에 알리는 유일한 길이다(cpp-script-plan §3.2). `GameScriptBase` 에서 바로 파생한 타입은 여기서
    // 컴파일이 실패한다 - 통과시키면 화면 UI 가 그 객체를 `GameScript3D` 로 불러 정의되지 않은 동작이 된다.
    template<typename T>
    Bool RegisterScriptType3D()
    {
        static_assert(std::is_base_of_v<GameScript3D, T>,
            "a 3D script must derive from JBro::GameScript3D, not GameScriptBase");
        return RegisterScriptType<T>();
    }
}

// 3D 스크립트 하나를 등록한다. 그 스크립트의 `.cpp` 에 한 줄 쓴다(2D 의 `JBRO_REGISTER_SCRIPT_2D` 와 같다).
#define JBRO_REGISTER_SCRIPT_3D(Type)                                                          \
    static ::JBro::ScriptTypeRegistration JBRO_SCRIPT_CONCAT(JBroScriptRegistration_, __LINE__)( \
        []() -> ::JBro::Bool                                                                   \
        {                                                                                      \
            return ::JBro::RegisterScriptType3D<Type>();                                       \
        })
