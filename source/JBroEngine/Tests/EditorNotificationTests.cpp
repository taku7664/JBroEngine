#include <JBro/Core/Log.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorNotifications.h>
#include <JBro/Editor/Widget/Notification.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <stdexcept>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 우측 하단 알림(todo "에디터 공용 기반" 1 번).
//
// 앞 절은 **ImGui 없이** 더미의 판단(큐·시간·애니메이션·끌어서 밀기·합치기)을 재고, 뒤 절은 렌더러
// 없는 ImGui 컨텍스트에서 위젯이 그 판단을 손짓으로 제대로 잇는지 잰다.

namespace
{
    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    class QuietLog
    {
    public:
        QuietLog()
            : m_previous(JBro::Log::GetEchoToConsole())
        {
            JBro::Log::SetEchoToConsole(false);
        }
        ~QuietLog()
        {
            JBro::Log::SetEchoToConsole(m_previous);
        }
        QuietLog(const QuietLog&) = delete;
        QuietLog& operator=(const QuietLog&) = delete;

    private:
        JBro::Bool m_previous = true;
    };

    using JBro::EditorNotifications;
    using JBro::NotificationDesc;
    using JBro::NotificationHandle;
    using JBro::NotificationLevel;

    constexpr JBro::Float Frame = 1.0f / 60.0f;

    void Run(EditorNotifications& notifications, JBro::Float seconds)
    {
        const JBro::Int32 frames = static_cast<int>(std::ceil(seconds / Frame));
        for (JBro::Int32 frame = 0; frame < frames; ++frame)
        {
            notifications.Update(Frame);
        }
    }

    // 몇 번 불렸는지와 누가 불렀는지를 센다.
    class CountingAction final : public JBro::NotificationAction
    {
    public:
        explicit CountingAction(JBro::Int32& calls, JBro::EditorApplication** seen = nullptr)
            : m_calls(calls), m_seen(seen)
        {
        }

        void OnClick(JBro::EditorApplication& editor) override
        {
            ++m_calls;
            if (m_seen != nullptr)
            {
                *m_seen = &editor;
            }
        }

    private:
        JBro::Int32& m_calls;
        JBro::EditorApplication** m_seen = nullptr;
    };

    NotificationHandle NotifyWithAction(EditorNotifications& notifications, JBro::Int32& calls, JBro::Float duration = -1.0f)
    {
        NotificationDesc desc;
        desc.title = "with action";
        desc.durationSeconds = duration;
        desc.action = JBro::MakeOwnerPtr<CountingAction>(calls);
        return notifications.Notify(std::move(desc));
    }

    // 번호가 아니라 핸들로 찾는다. 앞의 것이 빠지면 번호가 밀린다(처음에 번호로 재다 엉뚱한 상자를 쟀다).
    JBro::NotificationView ViewOf(const EditorNotifications& notifications, NotificationHandle handle)
    {
        for (JBro::UInt32 index = 0; index < notifications.GetVisibleCount(); ++index)
        {
            const JBro::NotificationView view = notifications.GetVisible(index);
            if (view.handle == handle)
            {
                return view;
            }
        }
        Check(false, "the notification this test names must be on screen");
        return {};
    }

    // ── 더미의 판단 ─────────────────────────────────────────────────────

    // **큐다.** 다섯까지 뜨고 나머지는 기다리며, 하나가 빠지면 다음이 온 차례대로 들어온다.
    void TestOnlyFiveShowAndTheRestWaitInOrder()
    {
        QuietLog quiet;
        EditorNotifications notifications;
        NotificationHandle handles[7] = {};
        char title[16] = {};
        for (JBro::Int32 index = 0; index < 7; ++index)
        {
            std::snprintf(title, sizeof(title), "n%d", index.Get());
            handles[index] = notifications.Notify(NotificationLevel::Info, title);
            Check(handles[index] != JBro::InvalidNotificationHandle, "a titled notification must be accepted");
        }
        notifications.Update(Frame);
        Check(notifications.GetVisibleCount() == EditorNotifications::MaxVisible, "only five may be on screen");
        Check(notifications.GetPendingCount() == 2, "the other two must wait");
        Check(notifications.GetVisible(0).handle == handles[0], "the oldest comes first");
        Check(notifications.GetVisible(4).handle == handles[4], "and the fifth is the newest on screen");

        // **다 들어온 뒤에 닫는다.** 막 들어오던 것을 닫으면 한 프레임 만에 사라져, 사라지는 동안 자리를 쥐는지 가를 수 없다.
        Run(notifications, EditorNotifications::FadeSeconds + Frame);
        notifications.Dismiss(handles[1]);
        Check(false == notifications.IsAlive(handles[1]), "a dismissed one is closed at once");
        notifications.Update(Frame);
        // 사라지는 중인 것은 자리를 비운 것이다 - 다음 것이 곧바로 들어온다.
        Check(notifications.GetPendingCount() == 1, "the first waiting one must come in as soon as a slot frees");
        JBro::Bool sixthShown = false;
        for (JBro::UInt32 index = 0; index < notifications.GetVisibleCount(); ++index)
        {
            sixthShown = sixthShown || notifications.GetVisible(index).handle == handles[5];
        }
        Check(sixthShown, "and it must be the one that came first");
        Run(notifications, EditorNotifications::FadeSeconds + Frame * 2.0f);
        Check(notifications.GetVisibleCount() == EditorNotifications::MaxVisible, "the leaving one must be gone after its fade");
    }

    void TestAnUntitledNotificationIsRefused()
    {
        QuietLog quiet;
        EditorNotifications notifications;
        Check(notifications.Notify(NotificationLevel::Error, nullptr) == JBro::InvalidNotificationHandle,
            "a notification without a title says nothing and must be refused");
        Check(notifications.Notify(NotificationLevel::Error, "") == JBro::InvalidNotificationHandle,
            "an empty title too");
        notifications.Update(Frame);
        Check(notifications.GetVisibleCount() == 0, "nothing must show");
    }

    // 들어올 때 옆에서 미끄러져 오며 흐릿하다가, 다 들어오면 제자리에 또렷이 선다.
    void TestANewNotificationSlidesInFromTheRight()
    {
        QuietLog quiet;
        EditorNotifications notifications;
        const NotificationHandle handle = notifications.Notify(NotificationLevel::Info, "hello");
        notifications.Update(Frame);
        notifications.ReportLayout(handle, 300.0f, 0.0f);
        notifications.Update(Frame);
        const JBro::NotificationView entering = notifications.GetVisible(0);
        Check(entering.offsetX > 0.0f, "an entering box must stand off to the right");
        Check(entering.alpha > 0.0f && entering.alpha < 1.0f, "and be partly faded");
        Run(notifications, EditorNotifications::FadeSeconds);
        const JBro::NotificationView shown = notifications.GetVisible(0);
        Check(std::fabs(shown.offsetX) < 0.5f, "once in, it must rest at its place");
        Check(shown.alpha == 1.0f, "and be fully visible");
    }

    // 무게마다 기본 시간이 지나면 사라지고, 0 은 닫을 때까지 남는다.
    void TestTimedNotificationsLeaveAndStickyOnesStay()
    {
        QuietLog quiet;
        EditorNotifications notifications;
        const NotificationHandle info = notifications.Notify(NotificationLevel::Info, "info");
        NotificationDesc sticky;
        sticky.title = "sticky";
        sticky.durationSeconds = 0.0f;
        const NotificationHandle stays = notifications.Notify(std::move(sticky));
        const NotificationHandle error = notifications.Notify(NotificationLevel::Error, "error");

        Run(notifications, EditorNotifications::DefaultDuration(NotificationLevel::Info) - 0.5f);
        Check(notifications.IsAlive(info), "an info must stay until its time is up");
        Run(notifications, 1.0f + EditorNotifications::FadeSeconds);
        Check(false == notifications.IsAlive(info), "and leave after it");
        Check(notifications.IsAlive(error), "an error must stay longer than an info");
        Run(notifications, 60.0f);
        Check(false == notifications.IsAlive(error), "but it leaves in the end");
        Check(notifications.IsAlive(stays), "a notification with no duration must stay until closed");
        Check(notifications.GetVisibleCount() == 1, "the timed ones must be gone from the screen too");
    }

    // 올려 두거나 끄는 동안은 시간이 멈춘다.
    void TestHoveringOrDraggingStopsTheClock()
    {
        QuietLog quiet;
        EditorNotifications notifications;
        const NotificationHandle handle = notifications.Notify(NotificationLevel::Info, "read me");
        Run(notifications, EditorNotifications::FadeSeconds + Frame);
        notifications.SetHovered(handle, true);
        Run(notifications, 30.0f);
        Check(notifications.IsAlive(handle), "a hovered notification must not run out");
        notifications.SetHovered(handle, false);
        notifications.ReportLayout(handle, 300.0f, 0.0f);
        notifications.Drag(handle, 10.0f);
        Run(notifications, 30.0f);
        Check(notifications.IsAlive(handle), "nor one that is being dragged");
        notifications.Release(handle);
        Run(notifications, EditorNotifications::DefaultDuration(NotificationLevel::Info) + 1.0f);
        Check(false == notifications.IsAlive(handle), "let go of, its clock must run again");
    }

    // 멀리 끌어 놓으면 그쪽으로 밀려 나가고, 조금 끌어 놓으면 제자리로 돌아온다.
    void TestSwipingFarDismissesAndSwipingShortSnapsBack()
    {
        QuietLog quiet;
        EditorNotifications notifications;
        constexpr JBro::Float Width = 300.0f;
        const NotificationHandle left = notifications.Notify(NotificationLevel::Info, "swipe left");
        const NotificationHandle shortOne = notifications.Notify(NotificationLevel::Info, "swipe short");
        const NotificationHandle right = notifications.Notify(NotificationLevel::Info, "swipe right");
        Run(notifications, EditorNotifications::FadeSeconds + Frame);
        for (NotificationHandle handle : {left, shortOne, right})
        {
            notifications.ReportLayout(handle, Width, 0.0f);
        }

        notifications.Drag(left, -Width * 0.5f);
        notifications.Release(left);
        Check(false == notifications.IsAlive(left), "dragged half its width and let go, it must leave");
        const JBro::Float before = ViewOf(notifications, left).offsetX;
        notifications.Update(Frame);
        Check(ViewOf(notifications, left).offsetX < before, "and fly off to the left, the way it was pushed");

        notifications.Drag(right, Width * 0.5f);
        notifications.Release(right);
        notifications.Update(Frame);
        Check(false == notifications.IsAlive(right), "to the right works the same");
        Check(ViewOf(notifications, right).offsetX > Width * 0.5f, "and flies off to the right");

        notifications.Drag(shortOne, Width * EditorNotifications::SwipeDismissFraction * 0.5f);
        notifications.Release(shortOne);
        Check(notifications.IsAlive(shortOne), "a short drag must not dismiss");
        notifications.Update(Frame);
        Check(ViewOf(notifications, shortOne).offsetX > 1.0f, "let go, it starts from where it was dragged to");
        Run(notifications, 0.5f);
        Check(std::fabs(ViewOf(notifications, shortOne).offsetX) < 0.5f, "and snaps back to its place");
    }

    // 같은 Id 는 새로 쌓지 않고 떠 있는 것을 고쳐 쓴다.
    void TestTheSameIdUpdatesInsteadOfStacking()
    {
        QuietLog quiet;
        EditorNotifications notifications;
        NotificationDesc first;
        first.title = "asset reload failed";
        first.id = "reload";
        const NotificationHandle handle = notifications.Notify(std::move(first));
        Run(notifications, 3.0f);

        NotificationDesc second;
        second.title = "asset reload failed again";
        second.message = "hero.png";
        second.id = "reload";
        Check(notifications.Notify(std::move(second)) == handle, "the same id must hand back the same notification");
        notifications.Update(Frame);
        Check(notifications.GetVisibleCount() == 1, "and must not add a second box");
        const JBro::NotificationView view = notifications.GetVisible(0);
        Check(view.count == 2, "the count must go up");
        Check(std::strcmp(view.title, "asset reload failed again") == 0, "the text must be the new one");
        Check(std::strcmp(view.message, "hero.png") == 0, "the message too");
        Check(view.remainingFraction > 0.9f, "its clock must start again");

        Check(notifications.Notify(NotificationLevel::Info, "no id") != handle, "without an id it is a new one");
    }

    // 누르면 할 일을 한 번 부르고 닫는다. 이미 닫히는 것은 다시 부르지 않는다.
    void TestActivatingCallsTheActionOnceAndCloses()
    {
        QuietLog quiet;
        EditorNotifications notifications;
        JBro::Int32 calls = 0;
        JBro::EditorApplication* seen = nullptr;
        NotificationDesc desc;
        desc.title = "open the log";
        desc.action = JBro::MakeOwnerPtr<CountingAction>(calls, &seen);
        const NotificationHandle handle = notifications.Notify(std::move(desc));
        notifications.Update(Frame);
        Check(notifications.GetVisible(0).hasAction, "the view must say there is something to do");

        JBro::EditorApplication editor;
        notifications.Activate(handle, editor);
        Check(calls == 1, "activating must call the action");
        Check(seen == &editor, "with the editor that activated it");
        Check(false == notifications.IsAlive(handle), "and close the notification");
        notifications.Activate(handle, editor);
        Check(calls == 1, "a closing notification must not be activated twice");

        const NotificationHandle plain = notifications.Notify(NotificationLevel::Info, "plain");
        notifications.Update(Frame);
        notifications.Activate(plain, editor);
        Check(false == notifications.IsAlive(plain), "without an action, clicking just closes it");
    }

    void TestWaitingNotificationsCanBeDismissedBeforeTheyShow()
    {
        QuietLog quiet;
        EditorNotifications notifications;
        NotificationHandle last = JBro::InvalidNotificationHandle;
        for (JBro::Int32 index = 0; index < 6; ++index)
        {
            last = notifications.Notify(NotificationLevel::Info, "queued");
        }
        notifications.Update(Frame);
        Check(notifications.GetPendingCount() == 1, "the sixth must wait");
        notifications.Dismiss(last);
        Check(notifications.GetPendingCount() == 0, "a dismissed waiting one no longer waits");
        Check(false == notifications.IsAlive(last), "and is closed");
        notifications.Update(Frame);
        Check(notifications.GetVisibleCount() == EditorNotifications::MaxVisible, "it never shows - it is gone by the next update");
        notifications.DismissAll();
        Check(notifications.GetPendingCount() == 0, "dismiss all empties the queue");
        Run(notifications, EditorNotifications::FadeSeconds + Frame * 2.0f);
        Check(notifications.GetVisibleCount() == 0, "and clears the screen after the fade");
    }

    // 알림은 몇 초 뒤 사라지므로 같은 말이 로그에도 남아야 한다. 끄면 남지 않는다.
    void TestNotificationsAreWrittenToTheLog()
    {
        QuietLog quiet;
        JBro::Log::Clear();
        EditorNotifications notifications;
        notifications.Notify(NotificationLevel::Warning, "watch stopped", "the folder is gone");
        Check(JBro::Log::GetCount() == 1, "a notification must leave a log line");
        const JBro::LogEntry* entry = JBro::Log::GetAt(0);
        Check(entry != nullptr && entry->level == JBro::LogLevel::Warning, "at the notification's level");
        Check(std::strstr(entry->message, "watch stopped") != nullptr
            && std::strstr(entry->message, "the folder is gone") != nullptr, "with its title and message");

        NotificationDesc silent;
        silent.title = "silent";
        silent.writeLog = false;
        notifications.Notify(std::move(silent));
        Check(JBro::Log::GetCount() == 1, "turning the log off must leave no line");
    }

    // 쌓인 자리를 옮기면 한 번에 뛰지 않고 따라간다. 처음 알린 자리에는 곧바로 선다.
    void TestTheStackFollowsItsTargetSmoothly()
    {
        QuietLog quiet;
        EditorNotifications notifications;
        const NotificationHandle handle = notifications.Notify(NotificationLevel::Info, "moving");
        notifications.Update(Frame);
        notifications.ReportLayout(handle, 300.0f, 40.0f);
        Check(notifications.GetVisible(0).offsetY == 40.0f, "the first reported place is taken at once");
        notifications.ReportLayout(handle, 300.0f, 140.0f);
        notifications.Update(Frame);
        const JBro::Float step = notifications.GetVisible(0).offsetY;
        Check(step > 40.0f && step < 140.0f, "a new place must be approached, not jumped to");
        Run(notifications, 1.0f);
        Check(std::fabs(notifications.GetVisible(0).offsetY - 140.0f) < 0.5f, "and reached in the end");
    }

    // ── 위젯 ───────────────────────────────────────────────────────────

    constexpr JBro::Float DisplayWidth = 1000.0f;
    constexpr JBro::Float DisplayHeight = 700.0f;

    class Stage
    {
    public:
        Stage()
        {
            m_context = ImGui::CreateContext();
            ImGui::SetCurrentContext(m_context);
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(DisplayWidth, DisplayHeight);
            io.DeltaTime = Frame;
            io.IniFilename = nullptr;
            io.Fonts->AddFontDefault();
            io.Fonts->Build();
        }
        ~Stage()
        {
            ImGui::DestroyContext(m_context);
        }
        Stage(const Stage&) = delete;
        Stage& operator=(const Stage&) = delete;

        // 한 프레임: 더미를 옮기고, 다른 창 하나와 알림을 그린다. 누른 것을 돌려준다.
        NotificationHandle Step(EditorNotifications& notifications, JBro::Bool drawOther = true)
        {
            ImGui::NewFrame();
            if (drawOther)
            {
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(DisplayWidth, DisplayHeight));
                ImGui::Begin("Background");
                ImGui::End();
            }
            notifications.Update(Frame);
            const NotificationHandle clicked = JBro::Widget::NotificationStack(notifications);
            ImGui::Render();
            return clicked;
        }

        void MoveMouse(JBro::Float x, JBro::Float y)
        {
            ImGui::GetIO().AddMousePosEvent(x, y);
        }

        void Button(JBro::Bool down)
        {
            ImGui::GetIO().AddMouseButtonEvent(0, down);
        }

    private:
        ImGuiContext* m_context = nullptr;
    };

    ImGuiWindow* BoxOf(NotificationHandle handle)
    {
        char name[48] = {};
        std::snprintf(name, sizeof(name), "##notification_%llu", static_cast<unsigned long long>(handle));
        return ImGui::FindWindowByName(name);
    }

    void Settle(Stage& stage, EditorNotifications& notifications, JBro::Float seconds)
    {
        const JBro::Int32 frames = static_cast<int>(std::ceil(seconds / Frame));
        for (JBro::Int32 frame = 0; frame < frames; ++frame)
        {
            stage.Step(notifications);
        }
    }

    // 우측 하단에 서고, 가장 새 것이 바닥이며, 새 것이 오면 앞의 것이 위로 올라간다.
    void TestBoxesStackUpFromTheBottomRight()
    {
        QuietLog quiet;
        Stage stage;
        EditorNotifications notifications;
        const JBro::Widget::NotificationStackStyle style;
        const NotificationHandle first = notifications.Notify(NotificationLevel::Info, "first");
        Settle(stage, notifications, 1.0f);
        ImGuiWindow* box = BoxOf(first);
        Check(box != nullptr && box->Active, "the notification must be drawn as a box");
        Check(std::fabs(box->Pos.x + box->Size.x - (DisplayWidth - style.margin)) < 1.0f, "its right edge sits a margin from the right");
        Check(std::fabs(box->Pos.y + box->Size.y - (DisplayHeight - style.margin)) < 1.0f, "its bottom edge a margin from the bottom");
        const JBro::Float restingY = box->Pos.y;

        const NotificationHandle second = notifications.Notify(NotificationLevel::Error, "second", "with a message");
        stage.Step(notifications);
        stage.Step(notifications);
        const JBro::Float risingY = BoxOf(first)->Pos.y;
        Settle(stage, notifications, 1.0f);
        ImGuiWindow* older = BoxOf(first);
        ImGuiWindow* newer = BoxOf(second);
        Check(newer != nullptr && std::fabs(newer->Pos.y + newer->Size.y - (DisplayHeight - style.margin)) < 1.0f,
            "the new one takes the bottom");
        Check(std::fabs(older->Pos.y + older->Size.y + style.spacing - newer->Pos.y) < 1.0f,
            "the older one is pushed up to sit right above it");
        Check(risingY < restingY && risingY > older->Pos.y, "and it rises there smoothly, not in one jump");
        Check(newer->Size.y > older->Size.y, "a box with a message is taller than one without");
    }

    // 아래 상자가 사라지는 동안 위의 상자가 **미리** 내려온다. 사라진 뒤에 한꺼번에 내려오면 빈자리가 번쩍인다.
    void TestTheBoxAboveComesDownWhileTheOneBelowLeaves()
    {
        QuietLog quiet;
        Stage stage;
        EditorNotifications notifications;
        const NotificationHandle upper = notifications.Notify(NotificationLevel::Info, "upper");
        const NotificationHandle lower = notifications.Notify(NotificationLevel::Info, "lower");
        Settle(stage, notifications, 1.0f);
        const JBro::Float raisedY = BoxOf(upper)->Pos.y;
        notifications.Dismiss(lower);
        // 사라지는 데 걸리는 시간의 절반. 아직 아래 상자가 남아 있다.
        const JBro::Int32 half = static_cast<int>(EditorNotifications::FadeSeconds / Frame / 2.0f);
        for (JBro::Int32 frame = 0; frame < half; ++frame)
        {
            stage.Step(notifications);
        }
        Check(notifications.GetVisibleCount() == 2, "the lower box is still fading");
        Check(BoxOf(upper)->Pos.y > raisedY + 2.0f, "and the upper one is already on its way down");
    }

    // 도크의 창을 눌러 앞으로 가져와도 알림은 그 위에 있다.
    void TestBoxesStayInFrontOfOtherWindows()
    {
        QuietLog quiet;
        Stage stage;
        EditorNotifications notifications;
        const NotificationHandle handle = notifications.Notify(NotificationLevel::Info, "on top");
        Settle(stage, notifications, 0.5f);
        // 배경 창을 눌러 앞으로 가져온다.
        stage.MoveMouse(20.0f, 20.0f);
        stage.Step(notifications);
        stage.Button(true);
        stage.Step(notifications);
        stage.Button(false);
        stage.Step(notifications);
        const ImVector<ImGuiWindow*>& windows = ImGui::GetCurrentContext()->Windows;
        Check(windows.Size > 0 && windows[windows.Size - 1] == BoxOf(handle),
            "the notification must be drawn in front of everything");
    }

    // 끌지 않고 누르면 핸들이 돌아온다. 올려 둔 동안은 시간이 멈춘다.
    void TestClickingABoxHandsBackItsHandle()
    {
        QuietLog quiet;
        Stage stage;
        EditorNotifications notifications;
        JBro::Int32 calls = 0;
        const NotificationHandle handle = NotifyWithAction(notifications, calls);
        Settle(stage, notifications, 0.5f);
        ImGuiWindow* box = BoxOf(handle);
        // 가운데 아래쪽을 누른다 - 오른쪽 위의 닫기 표시를 비킨다.
        const JBro::Float x = box->Pos.x + box->Size.x * 0.4f;
        const JBro::Float y = box->Pos.y + box->Size.y * 0.7f;
        stage.MoveMouse(x, y);
        stage.Step(notifications);
        Settle(stage, notifications, EditorNotifications::DefaultDuration(NotificationLevel::Info) + 1.0f);
        Check(notifications.IsAlive(handle), "a hovered box must not time out");

        stage.Button(true);
        Check(stage.Step(notifications) == JBro::InvalidNotificationHandle, "pressing alone is not a click");
        stage.Button(false);
        const NotificationHandle clicked = stage.Step(notifications);
        Check(clicked == handle, "letting go on the box must hand back its handle");
        JBro::EditorApplication editor;
        notifications.Activate(clicked, editor);
        Check(calls == 1, "and activating it calls its action");
    }

    // 누른 채 상자 밖으로 나가 떼면 누른 것이 아니다 - 보통 단추와 같다. 끌기 문턱 아래로만 움직여 끌기와 가른다.
    void TestReleasingOutsideTheBoxIsNotAClick()
    {
        QuietLog quiet;
        Stage stage;
        EditorNotifications notifications;
        JBro::Int32 calls = 0;
        const NotificationHandle handle = NotifyWithAction(notifications, calls, 0.0f);
        Settle(stage, notifications, 0.5f);
        ImGuiWindow* box = BoxOf(handle);
        const JBro::Float threshold = ImGui::GetIO().MouseDragThreshold;
        const JBro::Float x = box->Pos.x + box->Size.x * 0.4f;
        const JBro::Float edgeY = box->Pos.y + 1.0f;
        stage.MoveMouse(x, edgeY);
        stage.Step(notifications);
        stage.Button(true);
        stage.Step(notifications);
        stage.MoveMouse(x, edgeY - threshold * 0.6f);
        stage.Step(notifications);
        stage.Button(false);
        Check(stage.Step(notifications) == JBro::InvalidNotificationHandle, "letting go outside the box must not click it");
        Check(notifications.IsAlive(handle), "and must leave it open");
    }

    // 누른 채 옆으로 끌면 상자가 따라오고, 멀리 끌어 놓으면 사라진다. 누른 것으로 치지 않는다.
    void TestDraggingABoxSidewaysSwipesItAway()
    {
        QuietLog quiet;
        Stage stage;
        EditorNotifications notifications;
        JBro::Int32 calls = 0;
        const NotificationHandle handle = NotifyWithAction(notifications, calls);
        Settle(stage, notifications, 0.5f);
        ImGuiWindow* box = BoxOf(handle);
        const JBro::Float restX = box->Pos.x;
        const JBro::Float x = box->Pos.x + box->Size.x * 0.4f;
        const JBro::Float y = box->Pos.y + box->Size.y * 0.7f;
        stage.MoveMouse(x, y);
        stage.Step(notifications);
        stage.Button(true);
        stage.Step(notifications);
        JBro::Bool clicked = false;
        for (JBro::Int32 step = 1; step <= 10; ++step)
        {
            stage.MoveMouse(x - 20.0f * static_cast<float>(step), y);
            clicked = stage.Step(notifications) != JBro::InvalidNotificationHandle || clicked;
        }
        Check(BoxOf(handle)->Pos.x < restX - 100.0f, "the box must follow the pointer while dragged");
        stage.Button(false);
        clicked = stage.Step(notifications) != JBro::InvalidNotificationHandle || clicked;
        Check(false == clicked, "a swipe is not a click");
        Check(false == notifications.IsAlive(handle), "swiped far and let go, it must leave");
        Settle(stage, notifications, 0.5f);
        Check(notifications.GetVisibleCount() == 0, "and be gone once it has flown off");
        Check(calls == 0, "its action must not run");
    }

    // 올려 두면 닫기 표시가 보이고, 누르면 닫힌다.
    void TestTheCloseMarkDismisses()
    {
        QuietLog quiet;
        Stage stage;
        EditorNotifications notifications;
        JBro::Int32 calls = 0;
        NotificationDesc desc;
        desc.title = "close me";
        desc.durationSeconds = 0.0f;
        desc.action = JBro::MakeOwnerPtr<CountingAction>(calls);
        const NotificationHandle handle = notifications.Notify(std::move(desc));
        Settle(stage, notifications, 0.5f);
        ImGuiWindow* box = BoxOf(handle);
        const JBro::Widget::NotificationStackStyle style;
        const JBro::Float closeSize = ImGui::GetTextLineHeight();
        const JBro::Float x = box->Pos.x + box->Size.x - style.padding - closeSize * 0.5f;
        const JBro::Float y = box->Pos.y + style.padding + closeSize * 0.5f;
        stage.MoveMouse(x, y);
        stage.Step(notifications);
        stage.Step(notifications);
        stage.Button(true);
        stage.Step(notifications);
        stage.Button(false);
        const NotificationHandle clicked = stage.Step(notifications);
        Check(false == notifications.IsAlive(handle), "clicking the close mark must dismiss");
        Check(clicked == JBro::InvalidNotificationHandle, "and must not count as clicking the box");
        Check(calls == 0, "so the action does not run");
    }

    // ── 실제 에디터 ────────────────────────────────────────────────────

    HWND FindOwnEditorWindow()
    {
        struct Search
        {
            DWORD process = 0;
            HWND found = nullptr;
        } search{GetCurrentProcessId(), nullptr};
        EnumWindows(
            [](HWND hwnd, LPARAM param) -> BOOL {
                Search& state = *reinterpret_cast<Search*>(param);
                DWORD owner = 0;
                GetWindowThreadProcessId(hwnd, &owner);
                wchar_t name[64] = {};
                wchar_t title[64] = {};
                GetClassNameW(hwnd, name, 64);
                GetWindowTextW(hwnd, title, 64);
                if (owner == state.process && std::wcscmp(name, L"JBroEngineWindow") == 0
                    && std::wcscmp(title, L"JBro Editor") == 0)
                {
                    state.found = hwnd;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        return search.found;
    }

    // 에디터에서 알리면 우측 하단에 뜨고, 누르면 그 에디터로 할 일이 불린다.
    void TestTheEditorShowsAndActivatesNotifications()
    {
        QuietLog quiet;
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 900;
        config.windowHeight = 640;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; editor notifications not verified" << std::endl;
            return;
        }
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");
        for (JBro::Int32 frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick");
        }

        JBro::Int32 calls = 0;
        JBro::EditorApplication* seen = nullptr;
        NotificationDesc desc;
        desc.level = NotificationLevel::Success;
        desc.title = "saved";
        desc.durationSeconds = 0.0f;
        desc.action = JBro::MakeOwnerPtr<CountingAction>(calls, &seen);
        const NotificationHandle handle = editor.GetNotifications().Notify(std::move(desc));
        for (JBro::Int32 frame = 0; frame < 30; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick");
        }
        ImGuiWindow* box = BoxOf(handle);
        Check(box != nullptr && box->Active, "the editor must draw the notification");
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const JBro::Widget::NotificationStackStyle style;
        Check(std::fabs(box->Pos.x + box->Size.x - (viewport->WorkPos.x + viewport->WorkSize.x - style.margin)) < 1.0f,
            "at the right of the editor window");
        // 바닥은 상태 표시줄 위다(D-236). 그 줄의 마지막 알림 자리를 덮지 않게 에디터가 그 높이만큼 올려 세운다.
        const ImGuiWindow* statusBar = ImGui::FindWindowByName("##EditorStatusBar");
        Check(statusBar != nullptr, "the editor must have a status bar");
        Check(std::fabs(box->Pos.y + box->Size.y
                  - (viewport->WorkPos.y + viewport->WorkSize.y - style.margin - statusBar->Size.y)) < 1.0f,
            "at its bottom, above the status bar");
        const ImVector<ImGuiWindow*>& windows = ImGui::GetCurrentContext()->Windows;
        Check(windows[windows.Size - 1] == box, "in front of the docked panels");

        const JBro::Int32 x = static_cast<int>(box->Pos.x + box->Size.x * 0.4f);
        const JBro::Int32 y = static_cast<int>(box->Pos.y + box->Size.y * 0.7f);
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        Check(editor.Tick(Frame), "the editor must tick");
        Check(calls == 1, "clicking it in the editor must run its action");
        Check(seen == &editor, "with that editor");
        Check(false == editor.GetNotifications().IsAlive(handle), "and close it");
        editor.Shutdown();
    }
}

JBro::Int32 RunEditorNotificationTests()
{
    TestOnlyFiveShowAndTheRestWaitInOrder();
    TestAnUntitledNotificationIsRefused();
    TestANewNotificationSlidesInFromTheRight();
    TestTimedNotificationsLeaveAndStickyOnesStay();
    TestHoveringOrDraggingStopsTheClock();
    TestSwipingFarDismissesAndSwipingShortSnapsBack();
    TestTheSameIdUpdatesInsteadOfStacking();
    TestActivatingCallsTheActionOnceAndCloses();
    TestWaitingNotificationsCanBeDismissedBeforeTheyShow();
    TestNotificationsAreWrittenToTheLog();
    TestTheStackFollowsItsTargetSmoothly();
    TestBoxesStackUpFromTheBottomRight();
    TestTheBoxAboveComesDownWhileTheOneBelowLeaves();
    TestBoxesStayInFrontOfOtherWindows();
    TestClickingABoxHandsBackItsHandle();
    TestReleasingOutsideTheBoxIsNotAClick();
    TestDraggingABoxSidewaysSwipesItAway();
    TestTheCloseMarkDismisses();
    TestTheEditorShowsAndActivatesNotifications();
    std::cout << "Editor notification tests passed.\n";
    return 0;
}
