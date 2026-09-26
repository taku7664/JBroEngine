#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>

#include <cstdint>

namespace JBro
{
    class EditorApplication;

    // 알림을 밖에서 가리키는 번호다. 포인터를 들고 있으면 사라진 뒤에 헛돈다(팝업과 같은 이유).
    using NotificationHandle = std::uint64_t;
    inline constexpr NotificationHandle InvalidNotificationHandle = 0;

    // 알림의 무게다. 색과 기본 표시 시간이 여기서 나온다.
    enum class NotificationLevel : std::uint8_t
    {
        Info,
        Success,
        Warning,
        Error
    };

    // 알림을 눌렀을 때 할 일이다. 팝업과 같이 `std::function` 이 아니라 가상 함수다 -
    // 알림이 들고 갈 상태는 파생 클래스의 멤버가 자연스럽다.
    class NotificationAction
    {
    public:
        NotificationAction() = default;
        virtual ~NotificationAction() = default;
        NotificationAction(const NotificationAction&) = delete;
        NotificationAction& operator=(const NotificationAction&) = delete;

        virtual void OnClick(EditorApplication& editor) = 0;
    };

    struct NotificationDesc
    {
        NotificationLevel level = NotificationLevel::Info;
        // 이미 번역된 글자다 - 어디에 쓰이는지는 부르는 쪽이 안다(§11.2). 제목은 비울 수 없다.
        const char* title = nullptr;
        // 둘째 줄부터의 설명이다. 비워도 된다. 상자 폭에서 줄을 바꾼다.
        const char* message = nullptr;
        // 같은 Id 의 알림이 떠 있거나 기다리고 있으면 새로 쌓지 않고 그것을 고쳐 쓴다 - 글자를 바꾸고
        // 시간을 처음부터 다시 세고 횟수를 하나 올린다. 같은 경고가 스무 번 쌓이면 다른 알림이 밀려난다.
        // nullptr 이거나 빈 글자면 매번 새 알림이다.
        const char* id = nullptr;
        // 떠 있는 시간(초). 음수면 무게의 기본값(`DefaultDuration`), 0 이면 사용자가 닫을 때까지 남는다.
        float durationSeconds = -1.0f;
        // 로그에도 같은 말을 남긴다. 알림은 몇 초 뒤 사라지므로, 지나간 것을 찾을 곳이 있어야 한다.
        bool writeLog = true;
        // 누르면 부른다. 없으면 눌러도 닫히기만 한다.
        OwnerPtr<NotificationAction> action;
    };

    // 그리는 쪽이 한 프레임에 알아야 하는 것 전부다. 값만 담는다 - 그리는 동안 목록이 바뀌어도
    // 이 값은 헛돌지 않는다.
    struct NotificationView
    {
        NotificationHandle handle = InvalidNotificationHandle;
        NotificationLevel level = NotificationLevel::Info;
        const char* title = nullptr;
        const char* message = nullptr;
        // 같은 Id 로 몇 번 왔는가. 1 이면 표시하지 않는다.
        std::uint32_t count = 1;
        bool hasAction = false;
        // 쉬는 자리에서 가로로 밀린 거리(px, 오른쪽이 +). 들어올 때·끌 때·밀려 나갈 때 움직인다.
        float offsetX = 0.0f;
        // 쌓인 더미의 바닥에서 이 상자의 바닥까지 거리(px). 새 알림이 오면 부드럽게 올라간다.
        float offsetY = 0.0f;
        float alpha = 1.0f;
        // 더미에서 차지하는 높이의 몫(0~1). 사라지는 동안 **곧게** 줄어 위의 것들이 고르게 내려온다 -
        // 투명도의 곡선을 쓰면 절반이 지나도 거의 다 남아, 끝에 가서 한꺼번에 내려온다.
        float space = 1.0f;
        // 시간이 흐르는 중인가(올려 두거나 끄는 동안은 멈춘다). 0~1 로 남은 몫.
        float remainingFraction = 1.0f;
        bool timed = true;
        // 사라지는 중이다. 누를 수 없다.
        bool leaving = false;
    };

    // 에디터 우측 하단의 알림 더미다(2026-09-26 요청, todo "에디터 공용 기반" 1 번).
    //
    // **큐다.** 한 번에 `MaxVisible` 개까지 뜨고 나머지는 온 차례대로 기다린다. 뜬 것이 빠지면 다음
    // 것이 들어온다. 새 알림이 바닥에 들어오고 앞의 것들이 위로 밀려 올라간다.
    //
    // **그리지 않는다.** 시간·애니메이션·끌어서 밀기의 판단만 든다. 그리기는 `Widget::NotificationStack`
    // 이 이 값을 읽어서 하고, 손짓(올려 두기·끌기·누르기)을 돌려준다 - 그래서 ImGui 없이 잴 수 있다.
    //
    // **메인 스레드 전용이다.** 워커에서 알리려면 태스크의 메인 콜백에서 부른다.
    class EditorNotifications
    {
    public:
        static constexpr std::uint32_t MaxVisible = 5;
        // 들어오고 나가는 데 걸리는 시간(초).
        static constexpr float FadeSeconds = 0.18f;
        // 끌어서 이만큼(상자 폭에 대한 몫) 넘기고 놓으면 사라진다. 못 미치면 제자리로 돌아간다.
        static constexpr float SwipeDismissFraction = 0.35f;

        static float DefaultDuration(NotificationLevel level);

        EditorNotifications() = default;
        EditorNotifications(const EditorNotifications&) = delete;
        EditorNotifications& operator=(const EditorNotifications&) = delete;

        NotificationHandle Notify(NotificationDesc desc);
        // 흔한 모양의 줄임이다.
        NotificationHandle Notify(NotificationLevel level, const char* title, const char* message = nullptr);

        // 닫기 요청. 떠 있으면 사라지는 애니메이션을 거치고, 기다리던 것은 다음 `Update` 에서 빠진다.
        // 모르는 핸들은 무시한다.
        void Dismiss(NotificationHandle handle);
        void DismissAll();
        // 떠 있거나 기다리는 중이면 참이다. 사라지는 중인 것은 이미 닫힌 것으로 본다.
        bool IsAlive(NotificationHandle handle) const;
        // 마지막으로 받은 알림의 제목과 무게다. 상자가 사라진 뒤에도 남는다 - 상태 표시줄이 거기에 둔다(13 번).
        // 받은 것이 없으면 빈 글자다.
        const char* GetLastTitle() const;
        NotificationLevel GetLastLevel() const;

        // 매 프레임 한 번. 시간을 세고 애니메이션을 옮기고, 끝난 것을 빼고 기다리던 것을 들인다.
        void Update(float deltaTime);

        // ── 그리는 쪽이 부른다 ──────────────────────────────────────────
        //
        // 떠 있는 것(사라지는 중인 것 포함)만 센다. 0 번이 가장 오래된 것, 끝이 가장 새 것이다.
        std::uint32_t GetVisibleCount() const;
        NotificationView GetVisible(std::uint32_t index) const;
        // 기다리는 것의 수.
        std::uint32_t GetPendingCount() const;

        // 이 상자의 폭과 높이, 그리고 더미 바닥에서의 목표 거리를 알린다. 매 프레임 부른다.
        // 처음 알린 프레임에는 그 자리에 바로 선다(옆에서 들어오므로 세로로 미끄러지지 않는다).
        void ReportLayout(NotificationHandle handle, float width, float targetOffsetY);
        // 마우스가 위에 있는가. 올려 둔 동안은 시간이 멈춘다 - 읽는 중에 사라지면 안 된다.
        void SetHovered(NotificationHandle handle, bool hovered);
        // 누른 채 가로로 끈 거리(px). 끄는 동안은 손을 따라간다.
        void Drag(NotificationHandle handle, float dragX);
        // 끌던 것을 놓았다. 멀리 끌었으면 그쪽으로 밀려 나가고, 아니면 제자리로 돌아간다.
        void Release(NotificationHandle handle);
        // 눌렀다(끌지 않고 뗐다). 할 일이 있으면 부르고 닫는다.
        void Activate(NotificationHandle handle, EditorApplication& editor);

    private:
        enum class Phase : std::uint8_t
        {
            Waiting,
            Entering,
            Shown,
            Leaving
        };

        struct Entry
        {
            NotificationHandle handle = InvalidNotificationHandle;
            NotificationLevel level = NotificationLevel::Info;
            String title;
            String message;
            String id;
            OwnerPtr<NotificationAction> action;
            Phase phase = Phase::Waiting;
            float duration = 0.0f;
            float remaining = 0.0f;
            std::uint32_t count = 1;
            // 0~1. 들어올 때 오르고 나갈 때 내린다.
            float presence = 0.0f;
            float width = 0.0f;
            float offsetX = 0.0f;
            float offsetY = 0.0f;
            float targetOffsetY = 0.0f;
            // 나갈 때 밀려 가는 쪽(+1 오른쪽, -1 왼쪽).
            float leaveDirection = 1.0f;
            bool laidOut = false;
            bool hovered = false;
            bool dragging = false;
        };

        Entry* Find(NotificationHandle handle);
        const Entry* Find(NotificationHandle handle) const;
        void StartLeaving(Entry& entry, float direction);
        std::uint32_t CountOnScreen() const;

        // 온 차례대로다. 떠 있는 것과 기다리는 것이 섞여 있고 `phase` 가 가른다.
        Array<OwnerPtr<Entry>> m_entries;
        NotificationHandle m_nextHandle = 1;
        String m_lastTitle;
        NotificationLevel m_lastLevel = NotificationLevel::Info;
    };
}
