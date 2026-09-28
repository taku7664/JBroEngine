#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Platform/Input.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/NameTable.h>

#include <cstdint>

namespace JBro
{
    // 가이드 포커스(D-251, `tasks/guide-focus-plan.md`)의 모델이다. 반투명한 막에 대상 위젯 하나만 뚫고,
    // 그 자리만 입력을 받게 한다.
    //
    // **ImGui 를 모른다.** 알림(`EditorNotifications`)과 같이 판단만 들고 그리기는 위젯 계층이 한다.
    // 허용 영역은 그리는 쪽이 프레임마다 다시 채운다 - 사각형은 지난 프레임에 그린 자리다.

    // 가리킬 대상 하나다. ImGui ID 가 아니다 - 메뉴 ID 는 번역된 글자라 언어를 바꾸면 바뀌고, 계층의 줄 ID 는
    // 주소라 되돌리기 뒤에 바뀐다. 이름은 번역과 무관한 안정된 이름이고(`MakeNameId("menu.file")`),
    // 같은 이름이 여러 줄에 나오면 `key` 로 가른다(계층의 오브젝트는 `EditorObjectId`).
    struct GuideFocusTarget
    {
        NameId name = InvalidNameId;
        std::uint64_t key = 0;

        constexpr bool IsValid() const noexcept { return name != InvalidNameId; }
        constexpr bool operator==(const GuideFocusTarget& other) const noexcept
        {
            return name == other.name && key == other.key;
        }
    };

    // 대상에 이르는 경로다. **부모부터 적는다** - 패널, 그 안의 헤더, 그 안의 필드. 한 칸씩 연다.
    struct GuideFocusPath
    {
        static constexpr std::uint32_t Capacity = 8;

        GuideFocusTarget targets[Capacity] = {};
        std::uint32_t count = 0;

        // 가득 찼거나 대상이 비었으면 거짓이다. 조용히 잘라 내면 엉뚱한 부모에서 멈춘다.
        bool Push(const GuideFocusTarget& target);
        bool IsEmpty() const noexcept { return count == 0; }
    };

    class EditorGuideFocus
    {
    public:
        static constexpr std::uint32_t AllowedRectCapacity = 16;

        EditorGuideFocus() = default;
        EditorGuideFocus(const EditorGuideFocus&) = delete;
        EditorGuideFocus& operator=(const EditorGuideFocus&) = delete;

        // ── 수명 ─────────────────────────────────────────────────
        //
        // 경로가 비었으면 거짓이고 아무것도 바꾸지 않는다. 이미 켜져 있으면 새 경로로 처음부터 다시 간다.
        // 허용 영역은 비운 채로 시작한다 - 그리는 쪽이 채우기 전에는 어디도 누를 수 없다.
        bool Begin(const GuideFocusPath& path);
        void End();
        bool IsActive() const noexcept { return m_active; }
        const GuideFocusPath& GetPath() const noexcept { return m_path; }

        // ── 허용 영역 ─────────────────────────────────────────────
        //
        // 구멍·이 단계에서 열린 팝업·말풍선이다. 가득 차면 거짓이다.
        void ClearAllowedRects() noexcept;
        bool AddAllowedRect(const Rect& rect);
        std::uint32_t GetAllowedRectCount() const noexcept { return m_allowedCount; }
        const Rect& GetAllowedRect(std::uint32_t index) const;
        bool IsAllowed(const Vector2& point) const noexcept;
        // 글자 칸에 이름을 치는 단계처럼 키보드를 쓰는 단계만 켠다. 시작할 때 꺼진다.
        void SetKeyboardAllowed(bool allowed) noexcept { m_keyboardAllowed = allowed; }
        bool IsKeyboardAllowed() const noexcept { return m_keyboardAllowed; }

        // ── 입력 문 ───────────────────────────────────────────────
        //
        // 플랫폼 이벤트를 ImGui 와 게임에 넘길 것으로 거른다. `out` 은 비우고 채운다(용량은 그대로 둔다).
        // **꺼져 있어도 매 프레임 부른다.** 꺼져 있으면 그대로 넘기면서 눌린 버튼과 마우스 자리를 센다 -
        // 켜기 전에 누른 버튼의 뗌을 켠 뒤에도 넘겨야 하고, 켜자마자 누른 자리를 알아야 한다.
        //
        //  - 허용 영역 밖의 마우스 위치는 화면 밖(`HiddenPointer`)으로 바꾼다. 밖의 위젯이 올림 색을 띠면
        //    누를 수 있는 것처럼 보인다. 안에서 눌러 시작한 끌기가 이어지는 동안은 실제 자리를 넘긴다.
        //  - 밖의 누름·휠·터치 시작은 버린다. **넘긴 누름의 뗌은 늘 넘긴다** - 버리면 버튼이 눌린 채 남는다.
        //  - 키 누름과 글자는 키보드를 허용한 단계에서만 넘긴다. 뗌은 늘 넘긴다. Esc 누름은 넘기지 않고
        //    건너뛰기 요청으로 적는다(`ConsumeSkipRequest`).
        //  - 창 포커스는 늘 넘긴다 - 창을 떠날 때 눌린 키가 남지 않게 하는 것이 그것이다.
        //  - 꺼진 뒤 처음 거를 때 숨겼던 마우스를 실제 자리로 되돌리는 이동 하나를 앞에 넣는다.
        void FilterInput(JArrayView<InputEvent> events, Array<InputEvent>& out);
        // Esc 가 눌렸는지 돌려주고 지운다.
        bool ConsumeSkipRequest() noexcept;

        // ImGui 가 "마우스 없음" 으로 읽는 자리다(`-FLT_MAX`).
        static constexpr float HiddenPointer = -3.402823466e+38f;

    private:
        bool IsPointerAllowed() const noexcept;
        void ResetPointerState() noexcept;
        // 그대로 넘기는 이벤트로 눌린 것과 자리를 센다.
        void TrackPassed(const InputEvent& event) noexcept;

        GuideFocusPath m_path;
        Rect m_allowed[AllowedRectCapacity] = {};
        std::uint32_t m_allowedCount = 0;
        bool m_active = false;
        bool m_keyboardAllowed = false;
        bool m_skipRequested = false;

        // 마지막으로 본 실제 마우스 자리다. 막이 켜지기 전에 본 것도 쓴다.
        Vector2 m_pointer;
        bool m_pointerKnown = false;
        // 마지막으로 넘긴 위치가 화면 밖이다.
        bool m_pointerHidden = false;
        // 누름을 넘긴 버튼(`MouseButton` 차례의 비트)과 터치(포인터 번호 32 미만의 비트)다.
        std::uint32_t m_passedButtons = 0;
        std::uint32_t m_passedTouches = 0;
    };
}
