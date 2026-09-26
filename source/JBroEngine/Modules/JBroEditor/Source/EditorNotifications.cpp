#include <JBro/Editor/EditorNotifications.h>

#include <JBro/Core/Log.h>

#include <algorithm>
#include <cmath>

namespace JBro
{
    namespace
    {
        bool IsBlank(const char* text)
        {
            return text == nullptr || text[0] == '\0';
        }

        LogLevel ToLogLevel(NotificationLevel level)
        {
            switch (level)
            {
            case NotificationLevel::Warning:
                return LogLevel::Warning;
            case NotificationLevel::Error:
                return LogLevel::Error;
            case NotificationLevel::Info:
            case NotificationLevel::Success:
            default:
                return LogLevel::Info;
            }
        }

        void WriteLog(NotificationLevel level, const char* title, const char* message)
        {
            if (IsBlank(message))
            {
                Log::Write(ToLogLevel(level), "editor", "%s", title);
                return;
            }
            Log::Write(ToLogLevel(level), "editor", "%s - %s", title, message);
        }

        // 들어오는 상자가 옆에서 미끄러져 오는 거리. 폭을 아직 모르면(처음 그리기 전) 이만큼이다.
        constexpr float FallbackSlide = 64.0f;
        // 쌓인 자리를 따라가는 빠르기(초당). 클수록 빨리 선다.
        constexpr float StackFollowRate = 14.0f;
        // 끌다 놓았는데 못 미쳤을 때 제자리로 돌아가는 빠르기(초당).
        constexpr float SnapBackRate = 18.0f;

        float EaseOut(float t)
        {
            const float inverse = 1.0f - t;
            return 1.0f - inverse * inverse * inverse;
        }

        // 목표 쪽으로 한 걸음. 프레임 시간이 길어도 넘어가지 않는다.
        float Approach(float value, float target, float rate, float deltaTime)
        {
            const float step = std::min(1.0f, rate * deltaTime);
            return value + (target - value) * step;
        }
    }

    float EditorNotifications::DefaultDuration(NotificationLevel level)
    {
        // 경고와 오류는 더 오래 둔다 - 무엇이 잘못됐는지 읽고 무엇을 할지 정할 시간이 필요하다.
        switch (level)
        {
        case NotificationLevel::Warning:
        case NotificationLevel::Error:
            return 10.0f;
        case NotificationLevel::Info:
        case NotificationLevel::Success:
        default:
            return 5.0f;
        }
    }

    NotificationHandle EditorNotifications::Notify(NotificationLevel level, const char* title, const char* message)
    {
        NotificationDesc desc;
        desc.level = level;
        desc.title = title;
        desc.message = message;
        return Notify(std::move(desc));
    }

    NotificationHandle EditorNotifications::Notify(NotificationDesc desc)
    {
        if (IsBlank(desc.title))
        {
            return InvalidNotificationHandle;
        }
        const float duration = desc.durationSeconds < 0.0f ? DefaultDuration(desc.level) : desc.durationSeconds;
        if (desc.writeLog)
        {
            WriteLog(desc.level, desc.title, desc.message);
        }

        // 같은 Id 가 살아 있으면 새로 쌓지 않고 그것을 고친다. 사라지는 중인 것은 이미 닫힌 것이다.
        if (false == IsBlank(desc.id))
        {
            for (OwnerPtr<Entry>& owned : m_entries)
            {
                Entry& entry = *owned;
                if (entry.phase == Phase::Leaving || entry.id != desc.id)
                {
                    continue;
                }
                entry.level = desc.level;
                entry.title = desc.title;
                entry.message = IsBlank(desc.message) ? "" : desc.message;
                entry.duration = duration;
                entry.remaining = duration;
                ++entry.count;
                if (desc.action.Get() != nullptr)
                {
                    entry.action = std::move(desc.action);
                }
                return entry.handle;
            }
        }

        OwnerPtr<Entry> entry = MakeOwnerPtr<Entry>();
        entry->handle = m_nextHandle++;
        entry->level = desc.level;
        entry->title = desc.title;
        entry->message = IsBlank(desc.message) ? "" : desc.message;
        entry->id = IsBlank(desc.id) ? "" : desc.id;
        entry->action = std::move(desc.action);
        entry->duration = duration;
        entry->remaining = duration;
        const NotificationHandle handle = entry->handle;
        m_entries.Add(std::move(entry));
        return handle;
    }

    // 뜨지 않고 기다리던 것도 같은 길로 간다: 투명한 채(`presence` 0) 사라지는 중이 되어 다음 `Update` 에서 빠진다.
    // 곧바로 빼는 갈래를 따로 두었는데, 그리기 전에 늘 `Update` 가 돌아 화면에 차이가 없었다(뮤테이션에서 살아남았다).
    void EditorNotifications::Dismiss(NotificationHandle handle)
    {
        Entry* entry = Find(handle);
        if (entry != nullptr)
        {
            StartLeaving(*entry, 1.0f);
        }
    }

    void EditorNotifications::DismissAll()
    {
        for (OwnerPtr<Entry>& owned : m_entries)
        {
            StartLeaving(*owned, 1.0f);
        }
    }

    bool EditorNotifications::IsAlive(NotificationHandle handle) const
    {
        const Entry* entry = Find(handle);
        return entry != nullptr && entry->phase != Phase::Leaving;
    }

    void EditorNotifications::Update(float deltaTime)
    {
        const float dt = std::isfinite(deltaTime) && deltaTime > 0.0f ? deltaTime : 0.0f;

        for (std::size_t index = 0; index < m_entries.Size();)
        {
            Entry& entry = *m_entries[index];
            const float slide = entry.width > 0.0f ? entry.width : FallbackSlide;
            switch (entry.phase)
            {
            case Phase::Waiting:
                break;
            case Phase::Entering:
                entry.presence = std::min(1.0f, entry.presence + dt / FadeSeconds);
                if (false == entry.dragging)
                {
                    entry.offsetX = (1.0f - EaseOut(entry.presence)) * slide;
                }
                if (entry.presence >= 1.0f)
                {
                    entry.phase = Phase::Shown;
                }
                break;
            case Phase::Shown:
                if (false == entry.dragging)
                {
                    entry.offsetX = Approach(entry.offsetX, 0.0f, SnapBackRate, dt);
                }
                // **올려 두거나 끄는 동안은 시간이 멈춘다.** 읽는 중에 사라지면 안 된다.
                if (entry.duration > 0.0f && false == entry.hovered && false == entry.dragging)
                {
                    entry.remaining -= dt;
                    if (entry.remaining <= 0.0f)
                    {
                        entry.remaining = 0.0f;
                        StartLeaving(entry, 1.0f);
                    }
                }
                break;
            case Phase::Leaving:
                entry.presence = std::max(0.0f, entry.presence - dt / FadeSeconds);
                entry.offsetX += entry.leaveDirection * slide * dt / FadeSeconds;
                if (entry.presence <= 0.0f)
                {
                    m_entries.RemoveAt(index);
                    continue;
                }
                break;
            }
            if (entry.laidOut)
            {
                entry.offsetY = Approach(entry.offsetY, entry.targetOffsetY, StackFollowRate, dt);
            }
            ++index;
        }

        // 빈 자리만큼 기다리던 것을 온 차례대로 들인다. 들어오는 첫 모습은 옆에 비켜선 채 투명하다.
        std::uint32_t onScreen = CountOnScreen();
        for (OwnerPtr<Entry>& owned : m_entries)
        {
            if (onScreen >= MaxVisible)
            {
                break;
            }
            Entry& entry = *owned;
            if (entry.phase != Phase::Waiting)
            {
                continue;
            }
            entry.phase = Phase::Entering;
            entry.presence = 0.0f;
            entry.offsetX = entry.width > 0.0f ? entry.width : FallbackSlide;
            ++onScreen;
        }
    }

    std::uint32_t EditorNotifications::GetVisibleCount() const
    {
        std::uint32_t count = 0;
        for (const OwnerPtr<Entry>& owned : m_entries)
        {
            if (owned->phase != Phase::Waiting)
            {
                ++count;
            }
        }
        return count;
    }

    NotificationView EditorNotifications::GetVisible(std::uint32_t index) const
    {
        NotificationView view;
        std::uint32_t seen = 0;
        for (const OwnerPtr<Entry>& owned : m_entries)
        {
            const Entry& entry = *owned;
            if (entry.phase == Phase::Waiting)
            {
                continue;
            }
            if (seen++ != index)
            {
                continue;
            }
            view.handle = entry.handle;
            view.level = entry.level;
            view.title = entry.title.c_str();
            view.message = entry.message.c_str();
            view.count = entry.count;
            view.hasAction = entry.action.Get() != nullptr;
            view.offsetX = entry.offsetX;
            view.offsetY = entry.offsetY;
            view.alpha = EaseOut(entry.presence);
            view.timed = entry.duration > 0.0f;
            view.remainingFraction = view.timed ? entry.remaining / entry.duration : 1.0f;
            view.leaving = entry.phase == Phase::Leaving;
            view.space = view.leaving ? entry.presence : 1.0f;
            return view;
        }
        return view;
    }

    std::uint32_t EditorNotifications::GetPendingCount() const
    {
        std::uint32_t count = 0;
        for (const OwnerPtr<Entry>& owned : m_entries)
        {
            if (owned->phase == Phase::Waiting)
            {
                ++count;
            }
        }
        return count;
    }

    void EditorNotifications::ReportLayout(NotificationHandle handle, float width, float targetOffsetY)
    {
        Entry* entry = Find(handle);
        if (entry == nullptr || entry->phase == Phase::Waiting)
        {
            return;
        }
        // 처음 폭을 알게 된 프레임에, 들어오는 중이면 그 폭에 맞춰 비켜선 거리를 고친다.
        if (entry->width <= 0.0f && entry->phase == Phase::Entering && false == entry->dragging)
        {
            entry->offsetX = (1.0f - EaseOut(entry->presence)) * width;
        }
        entry->width = width;
        entry->targetOffsetY = targetOffsetY;
        if (false == entry->laidOut)
        {
            entry->offsetY = targetOffsetY;
            entry->laidOut = true;
        }
    }

    void EditorNotifications::SetHovered(NotificationHandle handle, bool hovered)
    {
        Entry* entry = Find(handle);
        if (entry != nullptr)
        {
            entry->hovered = hovered;
        }
    }

    void EditorNotifications::Drag(NotificationHandle handle, float dragX)
    {
        Entry* entry = Find(handle);
        if (entry == nullptr || entry->phase == Phase::Waiting || entry->phase == Phase::Leaving)
        {
            return;
        }
        entry->dragging = true;
        entry->offsetX = dragX;
    }

    void EditorNotifications::Release(NotificationHandle handle)
    {
        Entry* entry = Find(handle);
        if (entry == nullptr || false == entry->dragging)
        {
            return;
        }
        entry->dragging = false;
        const float width = entry->width > 0.0f ? entry->width : FallbackSlide;
        if (std::fabs(entry->offsetX) >= width * SwipeDismissFraction)
        {
            StartLeaving(*entry, entry->offsetX < 0.0f ? -1.0f : 1.0f);
        }
    }

    void EditorNotifications::Activate(NotificationHandle handle, EditorApplication& editor)
    {
        Entry* entry = Find(handle);
        if (entry == nullptr || entry->phase == Phase::Waiting || entry->phase == Phase::Leaving)
        {
            return;
        }
        // 먼저 닫고 부른다. 할 일 안에서 같은 알림을 다시 띄워도(같은 Id) 닫히는 것에 붙지 않는다.
        StartLeaving(*entry, 1.0f);
        if (entry->action.Get() != nullptr)
        {
            entry->action->OnClick(editor);
        }
    }

    EditorNotifications::Entry* EditorNotifications::Find(NotificationHandle handle)
    {
        for (OwnerPtr<Entry>& owned : m_entries)
        {
            if (owned->handle == handle)
            {
                return owned.Get();
            }
        }
        return nullptr;
    }

    const EditorNotifications::Entry* EditorNotifications::Find(NotificationHandle handle) const
    {
        for (const OwnerPtr<Entry>& owned : m_entries)
        {
            if (owned->handle == handle)
            {
                return owned.Get();
            }
        }
        return nullptr;
    }

    void EditorNotifications::StartLeaving(Entry& entry, float direction)
    {
        if (entry.phase == Phase::Leaving)
        {
            return;
        }
        entry.phase = Phase::Leaving;
        entry.leaveDirection = direction;
        entry.dragging = false;
        entry.hovered = false;
    }

    std::uint32_t EditorNotifications::CountOnScreen() const
    {
        // 사라지는 중인 것은 세지 않는다 - 그 자리는 이미 비었고, 다음 것이 곧바로 들어온다.
        std::uint32_t count = 0;
        for (const OwnerPtr<Entry>& owned : m_entries)
        {
            if (owned->phase == Phase::Entering || owned->phase == Phase::Shown)
            {
                ++count;
            }
        }
        return count;
    }
}
