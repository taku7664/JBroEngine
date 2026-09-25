#pragma once

#include <JBro/InputTypes/InputView.h>
#include <JBro/Platform/Input.h>

#include <cstdint>

namespace JBro
{
    // 창 클라이언트 좌표를 게임 화면 픽셀로 옮기는 값이다. 게임 화면 픽셀 = (클라이언트 - origin) * scale.
    // 게임 호스트는 창 전체가 게임 화면이라 기본값(그대로)이다. 에디터는 게임 뷰의 사각형과 렌더 타깃 크기에서 만든다.
    struct InputSurfaceMapping
    {
        float originX = 0.0f;
        float originY = 0.0f;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
    };
}

namespace JBro::System
{
    // 게임 입력을 접고 레이어 체인에 내려보낸다(D-201). 엔진이 소유한다(ProjectRule §7).
    //
    // **폴링하지 않는다.** 플랫폼이 모은 이벤트(D-62)를 프레임마다 한 번 접는다 - 기존 엔진은
    // `GetAsyncKeyState` 로 긁어서 한 프레임 안에 눌렀다 뗀 키를 잃었다.
    // 메인 스레드 전용이다.
    class InputSystem final
    {
    public:
        InputSystem() = default;
        InputSystem(const InputSystem&) = delete;
        InputSystem& operator=(const InputSystem&) = delete;

        // 이번 프레임을 연다. 지난 프레임의 눌림·뗌 수, 글자, 이동, 휠을 비우고 이벤트를 순서대로 접는다.
        // 입력을 받지 않는 프레임(에디터에서 게임 뷰가 포커스를 갖지 않을 때)도 빈 목록으로 불러야 한다 -
        // 그러지 않으면 지난 프레임의 `IsPressed` 가 남는다.
        void BeginFrame(JArrayView<InputEvent> events, const InputSurfaceMapping& mapping = {});

        const InputFrame& GetFrame() const;

    private:
        void Fold(const InputEvent& event, const InputSurfaceMapping& mapping);
        void ReleaseAll();

        InputFrame m_frame;
    };
}
