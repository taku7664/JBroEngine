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
    // 그 자리만 입력을 받게 한다. 대상이 닫힌 마디 안에 있으면 부모부터 한 칸씩 연다.
    //
    // **ImGui 를 모른다.** 알림(`EditorNotifications`)과 같이 판단과 시간만 들고 그리기는 위젯 계층
    // (`Widget/GuideFocus.h`)이 한다. 한 프레임의 차례는 이렇다.
    //
    //   FilterInput   ImGui 에 넣기 전에 입력을 거른다(허용 영역은 지난 프레임의 것)
    //   BeginFrame    이번 프레임의 보고를 비운다
    //   Report...     그리는 쪽이 경로에 든 대상을 그릴 때마다 자리와 열림을 알린다
    //   Update        보고를 보고 칸을 넘기고, 구멍을 옮기고, 다음 프레임의 허용 영역을 채운다

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

    // 에디터가 쓰는 대상 이름이다. 그리는 쪽과 가리키는 쪽이 같은 이름을 써야 만난다.
    namespace GuideFocusTargets
    {
        // 패널 창. `key` 는 패널의 `GetTitle()`(번역하지 않는 이름)의 `MakeNameId` 다.
        GuideFocusTarget Panel(const char* title);
        // 메뉴 막대의 메뉴와 그 항목. 이름은 로컬라이징 키에서 따온다(`menu.file`, `menu.build_game`).
        GuideFocusTarget Menu(const char* name);
        // 계층의 레이어 줄과 오브젝트 줄.
        GuideFocusTarget HierarchyLayer(std::uint64_t layerId);
        GuideFocusTarget HierarchyObject(std::uint64_t editorObjectId);
        // 인스펙터의 컴포넌트 머리, 그 컴포넌트의 맨 위 필드 줄, 컴포넌트 추가 칸.
        GuideFocusTarget InspectorComponent(std::uint64_t componentTypeId);
        GuideFocusTarget InspectorField(std::uint64_t componentTypeId, NameId fieldName);
        GuideFocusTarget InspectorAddComponent();
    }

    // 경로의 한 칸을 누가 여는가.
    enum class GuideFocusOpen : std::uint8_t
    {
        // 구멍이 닿아 잠시 머문 뒤 기구가 연다. 패널·탭·접는 머리·트리.
        Auto,
        // 사용자가 누를 때까지 기다린다. 메뉴·콤보·우클릭 메뉴 - ImGui 에 메뉴를 코드로 여는 길이 없다.
        User
    };

    // 대상에 이르는 경로다. **부모부터 적는다** - 패널, 그 안의 헤더, 그 안의 필드. 마지막 칸이 대상이다.
    struct GuideFocusPath
    {
        static constexpr std::uint32_t Capacity = 8;

        GuideFocusTarget targets[Capacity] = {};
        GuideFocusOpen open[Capacity] = {};
        std::uint32_t count = 0;

        // 가득 찼거나 대상이 비었으면 거짓이다. 조용히 잘라 내면 엉뚱한 부모에서 멈춘다.
        bool Push(const GuideFocusTarget& target, GuideFocusOpen opener = GuideFocusOpen::Auto);
        bool IsEmpty() const noexcept { return count == 0; }
        // 경로의 몇 번째인가. 없으면 `count`.
        std::uint32_t Find(const GuideFocusTarget& target) const noexcept;
    };

    // 말풍선에서 사람이 고른 것이다.
    enum class GuideFocusAction : std::uint8_t
    {
        None,
        Next,
        Skip
    };

    class EditorGuideFocus
    {
    public:
        static constexpr std::uint32_t AllowedRectCapacity = 16;
        static constexpr std::uint32_t PopupCapacity = 8;
        // 구멍이 닿은 뒤 스스로 열기 전에 머무는 시간(초). 어디로 들어가는지 볼 틈이다.
        static constexpr float DwellSeconds = 0.35f;
        // 지금 칸이 이만큼 한 번도 그려지지 않으면 경로가 끊긴 것이다(오브젝트가 지워졌다, 컴포넌트가 없다).
        static constexpr float BrokenSeconds = 0.5f;
        // 막이 나타나고 사라지는 시간(초). 알림과 같다.
        static constexpr float FadeSeconds = 0.18f;
        // 구멍이 대상보다 넓은 여백(픽셀). 테두리가 대상의 글자를 덮지 않는다.
        static constexpr float HolePadding = 4.0f;

        EditorGuideFocus() = default;
        EditorGuideFocus(const EditorGuideFocus&) = delete;
        EditorGuideFocus& operator=(const EditorGuideFocus&) = delete;

        // ── 수명 ─────────────────────────────────────────────────
        //
        // 경로가 비었으면 거짓이고 아무것도 바꾸지 않는다. 이미 켜져 있으면 새 경로로 처음부터 다시 간다 -
        // 구멍은 지난 자리에서 새 대상으로 옮겨 간다.
        // 허용 영역은 비운 채로 시작한다 - 그리는 쪽이 대상을 알리기 전에는 어디도 누를 수 없다.
        bool Begin(const GuideFocusPath& path);
        void End();
        bool IsActive() const noexcept { return m_active; }
        const GuideFocusPath& GetPath() const noexcept { return m_path; }

        // ── 허용 영역 ─────────────────────────────────────────────
        //
        // `Update` 가 프레임마다 다시 채운다(대상 · 이 경로에서 열린 팝업 · 말풍선). 시험과 특별한 자리는 직접 더한다.
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
        //  - 멈춘 동안(`SetPaused`)은 꺼진 것과 같이 그대로 넘긴다.
        void FilterInput(JArrayView<InputEvent> events, Array<InputEvent>& out);
        // Esc 가 눌렸는지 돌려주고 지운다.
        bool ConsumeSkipRequest() noexcept;

        // **모달이 떠 있는 동안 막을 걷는다**(D-251 (7)). 경로는 그대로 두고, 입력은 막지 않고, 막은 사라진다.
        // 모달은 사용자가 답해야 하는 것이라 막으면 에디터가 멈춘 것처럼 보인다. 풀리면 그 자리에서 잇는다.
        void SetPaused(bool paused) noexcept;
        bool IsPaused() const noexcept { return m_paused; }

        // ImGui 가 "마우스 없음" 으로 읽는 자리다(`-FLT_MAX`).
        static constexpr float HiddenPointer = -3.402823466e+38f;

        // ── 한 프레임의 보고 ──────────────────────────────────────
        //
        void BeginFrame() noexcept;
        // 경로에 든 대상을 그렸다. 경로에 없는 대상은 무시한다(켜져 있지 않아도 무시한다).
        // `opened` 는 그 칸이 열려 있어 안쪽이 그려지는가다(잎사귀는 뜻이 없다). `visible` 은 스크롤로
        // 잘리지 않았는가, `activated` 는 이번 프레임에 눌렸거나 편집을 마쳤는가다.
        void Report(const GuideFocusTarget& target, const Rect& rect, bool opened, bool visible, bool activated);
        // 이 경로가 켜진 뒤에 열린 팝업(메뉴·콤보·우클릭 메뉴)이다. 그 안도 누를 수 있어야 경로를 따라간다.
        void ReportPopup(const Rect& rect);
        // 말풍선의 자리다. 그 단추(다음·건너뛰기)는 늘 누를 수 있다.
        void ReportBalloon(const Rect& rect);
        // 이 대상을 이번 프레임에 열어야 하는가. 지나온 칸은 열린 채로 두고, 머묾이 끝난 지금 칸을 연다.
        bool ShouldOpen(const GuideFocusTarget& target) const noexcept;
        // 이 대상이 지금 칸인가. 잘려 있는지는 그리는 쪽이 알고, 잘려 있으면 그 자리로 굴린다.
        bool ShouldScrollTo(const GuideFocusTarget& target) const noexcept;
        // 머묾이 끝나 이 칸을 열어 달라고 하는 중인가. 패널처럼 `SetNextItemOpen` 으로 열 수 없는 것은
        // 부르는 쪽(에디터)이 이것을 보고 앞으로 꺼낸다.
        bool IsOpenRequested(const GuideFocusTarget& target) const noexcept;
        // 팝업 기준선. 켜진 뒤 첫 프레임에 그리는 쪽이 그때 열려 있던 팝업의 수를 적는다 - 그보다 뒤에 열린 것만 허용한다.
        bool NeedsPopupBaseline() const noexcept { return m_active && false == m_popupBaselineSet; }
        void SetPopupBaseline(std::uint32_t openPopups) noexcept;
        std::uint32_t GetPopupBaseline() const noexcept { return m_popupBaseline; }

        // 보고를 보고 한 프레임을 나아간다. 꺼져 있어도 부른다 - 막이 사라지는 동안 알파가 내려간다.
        void Update(float deltaTime);

        // ── 그리는 쪽이 읽는 것 ───────────────────────────────────
        //
        // 지금 가리키는 칸의 번호다. 경로 끝에 닿으면 `count - 1` 이다.
        std::uint32_t GetLevel() const noexcept { return m_level; }
        // 막의 짙기(0..1). 나타나고 사라지는 동안 움직인다. 0 이면 그리지 않는다.
        float GetVeilAlpha() const noexcept { return m_veilAlpha; }
        // 구멍이 있는가(지금 칸을 한 번이라도 보았는가)와 지금 구멍의 자리(애니메이션 중인 자리)다.
        bool HasHole() const noexcept { return m_holeKnown; }
        const Rect& GetHoleRect() const noexcept { return m_hole; }
        // 구멍이 지금 칸의 자리에 닿았는가.
        bool IsHoleSettled() const noexcept { return m_holeSettled; }
        // 테두리가 숨 쉬는 박자(0..1).
        float GetPulse() const noexcept;
        // 구멍이 대상에 들어와 자리 잡은 뒤로 흐른 시간이다. 말풍선이 미끄러져 들어오는 데 쓴다.
        float GetSettledSeconds() const noexcept { return m_settledSeconds; }
        // 경로 끝의 대상에 닿았는가.
        bool IsAtTarget() const noexcept;
        // 경로 끝의 대상이 눌렸는가. 읽으면 지운다.
        bool ConsumeActivated() noexcept;
        // 지금 칸이 `BrokenSeconds` 동안 그려지지 않아 경로가 끊겼다. 로그는 한 번 남긴다.
        bool IsBroken() const noexcept { return m_broken; }

    private:
        bool IsPointerAllowed() const noexcept;
        void ResetPointerState() noexcept;
        // 그대로 넘기는 이벤트로 눌린 것과 자리를 센다.
        void TrackPassed(const InputEvent& event) noexcept;
        void EnterLevel(std::uint32_t level) noexcept;

        struct Seen
        {
            Rect rect;
            bool seen = false;
            bool opened = false;
            bool visible = false;
            bool activated = false;
        };

        GuideFocusPath m_path;
        Rect m_allowed[AllowedRectCapacity] = {};
        std::uint32_t m_allowedCount = 0;
        bool m_active = false;
        bool m_paused = false;
        bool m_keyboardAllowed = false;
        bool m_skipRequested = false;

        // 이번 프레임의 보고.
        Seen m_seen[GuideFocusPath::Capacity] = {};
        Rect m_popups[PopupCapacity] = {};
        std::uint32_t m_popupCount = 0;
        Rect m_balloon;
        bool m_balloonSeen = false;
        std::uint32_t m_popupBaseline = 0;
        bool m_popupBaselineSet = false;

        // 걸음.
        std::uint32_t m_level = 0;
        // 머묾이 끝나 열어 달라고 한 칸. 없으면 `Capacity`.
        std::uint32_t m_openRequest = GuideFocusPath::Capacity;
        float m_dwell = 0.0f;
        float m_unseen = 0.0f;
        bool m_broken = false;
        bool m_activated = false;

        // 그림.
        Rect m_hole;
        Rect m_holeTarget;
        bool m_holeKnown = false;
        bool m_holeSettled = false;
        float m_settledSeconds = 0.0f;
        float m_veilAlpha = 0.0f;
        float m_time = 0.0f;

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
