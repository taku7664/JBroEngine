#pragma once

#include <JBro/InputTypes/InputView.h>

#include <cstddef>
#include <cstdint>

namespace JBro::System
{
    // 서비스가 호스트의 입력 시스템에 닿는 인터페이스다(ProjectRule §10.3). 스크립트는 이것을 보지 않는다 -
    // 프렐류드가 include 하지 않고, 서비스 .cpp 만 `InputSystemContext` 로 받아 부른다.
    class IInputSystem
    {
    public:
        // 레이어 체인이 다 돈 뒤 남은 입력이다(D-214). 체인이 돌기 전에는 이번 프레임 전체다.
        virtual const InputView& GetResidualView() const noexcept = 0;

        // 게임패드 진동(D-214). `seconds` 가 0 보다 크면 그만큼 뒤에 스스로 멈추고, 0 이면 멈추라고 할 때까지 돈다.
        // 창이 포커스를 잃거나 패드가 빠지면 멈춘다.
        virtual void SetGamepadVibration(std::uint32_t slot, float low, float high, float seconds) noexcept = 0;
        // 스틱의 둥근 데드존과 트리거의 문턱(0..0.95). 모든 패드에 같이 걸린다.
        virtual void SetGamepadDeadzones(float stick, float trigger) noexcept = 0;
        // 손가락을 스스로 만든다(D-214, 기존 엔진 `InjectTouch`). 화면 위 가상 조이스틱이나 자동 검사가 쓴다. 다음 프레임에 플랫폼의
        // 터치와 같은 길로 접힌다. 자리는 게임 화면 픽셀이다. 한 프레임에 16 개까지이고 넘치면 버린다.
        virtual void InjectTouch(std::uint32_t id, float x, float y, TouchPhase phase) noexcept = 0;

        // 액션 세트를 켜고 끈다(D-214). 곧바로 걸린다 - 이 프레임에 이 뒤로 읽는 액션부터다. 없는 세트면 거짓이고 한 번 경고한다.
        virtual bool SetActionSetEnabled(NameId set, bool enabled) noexcept = 0;
        virtual bool IsActionSetEnabled(NameId set) const noexcept = 0;

        // 런타임 리바인딩(D-218). 프로젝트의 표 위에 얹히고, 곧바로 걸린다. 없는 액션·자리면 거짓이다.
        virtual std::uint32_t GetActionBindingCount(InputActionId action) const noexcept = 0;
        virtual bool GetActionBinding(InputActionId action, std::uint32_t index, InputBinding& out) const noexcept = 0;
        // `index` 가 바인딩 수와 같으면 뒤에 붙인다(8 개까지).
        virtual bool SetActionBinding(InputActionId action, std::uint32_t index, const InputBinding& binding) noexcept = 0;
        virtual bool RemoveActionBinding(InputActionId action, std::uint32_t index) noexcept = 0;
        // 그 액션을 프로젝트의 바인딩으로 되돌린다.
        virtual bool ResetActionBindings(InputActionId action) noexcept = 0;
        virtual void ResetAllActionBindings() noexcept = 0;
        // 바꾼 것을 글자로 쓴다. 모자라면 거짓이고 `outSize` 에 필요한 바이트 수가 온다(끝의 0 은 세지 않는다).
        // 버퍼는 부르는 쪽(게임 DLL)이 키운다 - 호스트가 DLL 의 컨테이너를 키우지 않는다.
        virtual bool WriteBindingOverrides(char* buffer, std::size_t capacity, std::size_t& outSize) const noexcept = 0;
        // 글자를 읽어 바인딩을 바꾼다. 줄 하나라도 틀리면 거짓이고 그 액션은 그대로다.
        virtual bool ReadBindingOverrides(const char* text, std::size_t length) noexcept = 0;

    protected:
        ~IInputSystem() = default;
    };
}
