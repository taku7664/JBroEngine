#pragma once

#include <JBro/Core/Core.h>

#include <cstdint>

namespace JBro
{
    class EditorApplication;

    // 팝업을 밖에서 가리키는 번호다. 포인터를 들고 있으면 닫힌 뒤에 헛돈다(D-72 와 같은 이유).
    using PopupHandle = std::uint64_t;
    inline constexpr PopupHandle InvalidPopupHandle = 0;

    // 모달 팝업 하나다. 기존 엔진 `ImPopupDesc` + `CImPopupWindow` 를 옮겼다 - 핸들, 같은 Id 의
    // 중복 방지, 한 번에 하나만 뜨는 FIFO 큐는 그대로고, 세 콜백(`std::function`)은 패널(D-70)과
    // 같은 가상 함수로 바꿨다. 에디터 안에 `std::function` 을 쓰는 자리가 없고, 팝업이 들고 갈
    // 상태는 파생 클래스의 멤버가 자연스럽다.
    //
    // **한 번에 하나만 뜬다.** ImGui 의 `OpenPopup` / `BeginPopupModal` 은 스택으로 동작해 한
    // 프레임에 모달 하나만 정상적으로 열린다. 여럿을 열면 앞 것이 닫힌 다음 프레임에 다음 것이
    // 뜬다.
    class EditorPopup
    {
    public:
        EditorPopup() = default;
        virtual ~EditorPopup() = default;
        EditorPopup(const EditorPopup&) = delete;
        EditorPopup& operator=(const EditorPopup&) = delete;

        // 제목줄에 보이는 글자다. 번역한다(ProjectRule §11.2). 창의 정체는 핸들이 잡으므로
        // 제목이 바뀌어도 같은 팝업이다.
        virtual const char* GetTitle() const = 0;
        // 같은 Id 의 팝업이 살아 있으면 `OpenPopup` 은 새로 만들지 않고 그 핸들을 돌려준다.
        // nullptr 이거나 빈 글자면 매번 새 팝업이다.
        virtual const char* GetId() const
        {
            return nullptr;
        }
        // 거짓이면 제목줄의 X 가 없어 코드(`Close`)로만 닫힌다 - 진행 표시 같은 것.
        virtual bool HasCloseButton() const
        {
            return true;
        }
        // 처음 뜰 때의 크기. 0 이면 내용에 맞춘다.
        virtual float GetInitialWidth() const
        {
            return 0.0f;
        }
        virtual float GetInitialHeight() const
        {
            return 0.0f;
        }
        // 처음 그려지는 프레임에 한 번, 팝업 안에서.
        virtual void OnEnter(EditorApplication& editor)
        {
            (void)editor;
        }
        // 매 프레임, 팝업 안에서. `BeginPopupModal` 과 `EndPopup` 은 에디터가 부른다.
        virtual void OnDraw(EditorApplication& editor) = 0;
        // 닫힌 뒤 한 번. 팝업 밖이다.
        virtual void OnExit(EditorApplication& editor)
        {
            (void)editor;
        }

        // 닫기 요청. 다음 프레임에 `IsAlive` 가 거짓이 되고 큐에서 빠진다.
        void Close()
        {
            m_open = false;
        }

        bool IsAlive() const
        {
            return m_open;
        }

        PopupHandle GetHandle() const
        {
            return m_handle;
        }

    private:
        friend class EditorApplication;
        PopupHandle m_handle = InvalidPopupHandle;
        bool m_open = true;
        // 한 번이라도 그려졌는가. `OpenPopup` 을 부를 때와 `OnEnter` 를 부를 때를 가른다.
        bool m_shown = false;
    };
}
