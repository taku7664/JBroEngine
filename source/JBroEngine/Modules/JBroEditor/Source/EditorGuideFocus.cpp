#include <JBro/Editor/EditorGuideFocus.h>

#include <JBro/Core/Log.h>
#include <JBro/Types/Angle.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        constexpr UInt32 TouchBitCount = 32;
        // 구멍이 새 자리로 따라가는 빠르기다(지수 접근의 비율). 알림 더미가 따라가는 빠르기와 같다.
        constexpr Float HoleFollowRate = 14.0f;
        // 이만큼 가까워지면 닿은 것으로 본다(픽셀).
        constexpr Float HoleSettleDistance = 1.0f;
        // 처음 뜰 때 구멍은 대상보다 이만큼 넓은 데서 좁혀 들어온다(픽셀). 어디를 보라는지 눈이 따라간다.
        constexpr Float HoleIntroSpread = 160.0f;
        constexpr Float PulsePeriod = 1.2f;

        UInt32 ButtonBit(MouseButton button)
        {
            return 1u << static_cast<std::uint32_t>(button);
        }

        InputEvent MakeMove(Float x, Float y)
        {
            InputEvent move;
            move.kind = InputEventKind::MouseMove;
            move.x = x;
            move.y = y;
            return move;
        }

        Rect Inflate(const Rect& rect, Float by)
        {
            return Rect{ { rect.min.x - by, rect.min.y - by }, { rect.max.x + by, rect.max.y + by } };
        }

        Float Towards(Float value, Float target, Float blend)
        {
            return value + (target - value) * blend;
        }

        Float Farthest(const Rect& a, const Rect& b)
        {
            const Float dx0 = std::fabs(a.min.x - b.min.x);
            const Float dy0 = std::fabs(a.min.y - b.min.y);
            const Float dx1 = std::fabs(a.max.x - b.max.x);
            const Float dy1 = std::fabs(a.max.y - b.max.y);
            return std::fmax(std::fmax(dx0, dy0), std::fmax(dx1, dy1));
        }
    }

    namespace GuideFocusTargets
    {
        GuideFocusTarget Panel(const char* title)
        {
            return { MakeNameId("panel"), MakeNameId(title) };
        }

        GuideFocusTarget Menu(const char* name)
        {
            return { MakeNameId(name), 0 };
        }

        GuideFocusTarget HierarchyLayer(UInt64 layerId)
        {
            return { MakeNameId("hierarchy.layer"), layerId };
        }

        GuideFocusTarget HierarchyObject(UInt64 editorObjectId)
        {
            return { MakeNameId("hierarchy.object"), editorObjectId };
        }

        GuideFocusTarget InspectorComponent(UInt64 componentTypeId)
        {
            return { MakeNameId("inspector.component"), componentTypeId };
        }

        GuideFocusTarget InspectorField(UInt64 componentTypeId, NameId fieldName)
        {
            // 타입과 필드 이름을 한 열쇠로 섞는다. 같은 이름의 필드가 다른 컴포넌트에도 있다(`position`).
            return { MakeNameId("inspector.field"), (componentTypeId * 0x100000001B3ull) ^ fieldName };
        }

        GuideFocusTarget InspectorAddComponent()
        {
            return { MakeNameId("inspector.add_component"), 0 };
        }

        GuideFocusTarget Action(const char* name)
        {
            return { MakeNameId("action"), MakeNameId(name) };
        }

        GuideFocusTarget HierarchyObjectMenu(UInt64 editorObjectId)
        {
            return { MakeNameId("hierarchy.object_menu"), editorObjectId };
        }

        GuideFocusTarget CanvasViewObject(UInt64 editorObjectId)
        {
            return { MakeNameId("canvas_view.object"), editorObjectId };
        }

        GuideFocusTarget HierarchyBackground()
        {
            return { MakeNameId("hierarchy.background"), 0 };
        }

        GuideFocusTarget CanvasViewBackground()
        {
            return { MakeNameId("canvas_view.background"), 0 };
        }

        GuideFocusTarget ComponentListItem(UInt64 componentTypeId)
        {
            return { MakeNameId("component.list_item"), componentTypeId };
        }

        GuideFocusTarget ComponentCategoryMenu(const char* category)
        {
            return { MakeNameId("component.category_menu"), MakeNameId(category) };
        }

        GuideFocusTarget GizmoModeButton(UInt32 mode)
        {
            return { MakeNameId("canvas_view.gizmo_mode"), mode };
        }

        GuideFocusTarget GizmoHandle(UInt32 mode, UInt32 axis)
        {
            return { MakeNameId("canvas_view.gizmo_handle"), (static_cast<std::uint64_t>(mode) << 8) | axis };
        }

        GuideFocusTarget ColliderEditButton()
        {
            return { MakeNameId("canvas_view.edit_collider"), 0 };
        }

        GuideFocusTarget PolygonPoint(UInt32 index)
        {
            return { MakeNameId("canvas_view.polygon_point"), index };
        }
    }

    Bool GuideFocusPath::Push(const GuideFocusTarget& target, GuideFocusOpen opener)
    {
        if (false == target.IsValid() || count >= Capacity)
        {
            return false;
        }
        targets[count] = target;
        open[count] = opener;
        ++count;
        return true;
    }

    UInt32 GuideFocusPath::Find(const GuideFocusTarget& target) const noexcept
    {
        for (UInt32 index = 0; index < count; ++index)
        {
            if (targets[index] == target)
            {
                return index;
            }
        }
        return count;
    }

    Bool EditorGuideFocus::Begin(const GuideFocusPath& path)
    {
        if (path.IsEmpty())
        {
            return false;
        }
        m_path = path;
        m_active = true;
        m_keyboardAllowed = false;
        m_skipRequested = false;
        m_activated = false;
        m_popupBaselineSet = false;
        m_popupBaseline = 0;
        ClearAllowedRects();
        BeginFrame();
        EnterLevel(0);
        // 눌린 버튼은 그대로 둔다. 켜기 전에 누른 것은 ImGui 가 이미 누름을 보았으므로 그 뗌도 넘겨야 한다 -
        // 꺼져 있는 동안에도 거르기가 버튼을 세는 까닭이 이것이다.
        return true;
    }

    void EditorGuideFocus::End()
    {
        m_active = false;
        m_paused = false;
        m_path = {};
        m_keyboardAllowed = false;
        m_skipRequested = false;
        m_activated = false;
        ClearAllowedRects();
        BeginFrame();
        EnterLevel(0);
        // 눌린 버튼과 `m_pointerHidden` 은 남긴다 - 뗌은 계속 넘겨야 하고, 다음 거르기가 마우스를 실제 자리로 되돌린다.
        // 구멍도 남긴다 - 막이 사라지는 동안 그 자리에 뚫려 있어야 한다.
    }

    void EditorGuideFocus::EnterLevel(UInt32 level) noexcept
    {
        m_level = level;
        m_openRequest = GuideFocusPath::Capacity;
        m_dwell = 0.0f;
        m_unseen = 0.0f;
        m_broken = false;
        m_holeSettled = false;
        m_settledSeconds = 0.0f;
    }

    void EditorGuideFocus::SetPaused(Bool paused) noexcept
    {
        m_paused = m_active && paused;
    }

    void EditorGuideFocus::ClearAllowedRects() noexcept
    {
        m_allowedCount = 0;
    }

    Bool EditorGuideFocus::AddAllowedRect(const Rect& rect)
    {
        if (m_allowedCount >= AllowedRectCapacity)
        {
            return false;
        }
        m_allowed[m_allowedCount] = rect;
        ++m_allowedCount;
        return true;
    }

    const Rect& EditorGuideFocus::GetAllowedRect(UInt32 index) const
    {
        assert(index < m_allowedCount);
        return m_allowed[index];
    }

    Bool EditorGuideFocus::IsAllowed(const Vector2& point) const noexcept
    {
        for (UInt32 index = 0; index < m_allowedCount; ++index)
        {
            // **오른쪽과 아래 끝은 담지 않는다**(픽셀처럼 반열린 칸). `Rect::Contains` 는 끝을 담는데, 그러면 허용한 창의 바로
            // 아래 한 줄이 이웃한 창의 것인데도 열린다 - 말풍선 밑 한 줄에서 캔버스 뷰가 올림 색을 띠었다.
            // 뒤집힌 사각형과 넓이 0 인 사각형은 아무것도 담지 않는다.
            const Rect& rect = m_allowed[index];
            if (point.x >= rect.min.x && point.x < rect.max.x && point.y >= rect.min.y && point.y < rect.max.y)
            {
                return true;
            }
        }
        return false;
    }

    Bool EditorGuideFocus::IsPointerAllowed() const noexcept
    {
        return m_pointerKnown && IsAllowed(m_pointer);
    }

    void EditorGuideFocus::ResetPointerState() noexcept
    {
        m_passedButtons = 0;
        m_passedTouches = 0;
    }

    void EditorGuideFocus::TrackPassed(const InputEvent& event) noexcept
    {
        switch (event.kind)
        {
        case InputEventKind::MouseMove:
            m_pointer = Vector2{ event.x, event.y };
            m_pointerKnown = true;
            break;
        case InputEventKind::MouseButtonDown:
            m_passedButtons |= ButtonBit(event.button);
            break;
        case InputEventKind::MouseButtonUp:
            m_passedButtons &= ~ButtonBit(event.button);
            break;
        case InputEventKind::FocusLost:
            ResetPointerState();
            break;
        case InputEventKind::TouchBegan:
            if (event.codePoint < TouchBitCount)
            {
                m_passedTouches |= 1u << event.codePoint;
            }
            break;
        case InputEventKind::TouchEnded:
        case InputEventKind::TouchCancelled:
            if (event.codePoint < TouchBitCount)
            {
                m_passedTouches &= ~(1u << event.codePoint);
            }
            break;
        default:
            break;
        }
    }

    Bool EditorGuideFocus::ConsumeSkipRequest() noexcept
    {
        const Bool requested = m_skipRequested;
        m_skipRequested = false;
        return requested;
    }

    void EditorGuideFocus::FilterInput(JArrayView<InputEvent> events, Array<InputEvent>& out)
    {
        out.Clear();

        if (false == m_active || m_paused)
        {
            // 꺼진 뒤 처음이다. 숨겼던 마우스를 되돌리지 않으면 사용자가 움직일 때까지 ImGui 는 마우스가 없다고 본다.
            if (m_pointerHidden && m_pointerKnown)
            {
                out.Add(MakeMove(m_pointer.x, m_pointer.y));
            }
            m_pointerHidden = false;
            for (UInt32 index = 0; index < events.size; ++index)
            {
                TrackPassed(events.data[index]);
                out.Add(events.data[index]);
            }
            return;
        }

        for (UInt32 index = 0; index < events.size; ++index)
        {
            const InputEvent& event = events.data[index];
            switch (event.kind)
            {
            case InputEventKind::MouseMove:
            {
                m_pointer = Vector2{ event.x, event.y };
                m_pointerKnown = true;
                // 안에서 눌러 시작한 끌기는 밖으로 나가도 실제 자리를 받아야 한다 - 슬라이더가 멈춘다.
                if (m_passedButtons != 0 || IsAllowed(m_pointer))
                {
                    out.Add(event);
                    m_pointerHidden = false;
                }
                else
                {
                    out.Add(MakeMove(HiddenPointer, HiddenPointer));
                    m_pointerHidden = true;
                }
                break;
            }

            case InputEventKind::MouseButtonDown:
                if (IsPointerAllowed())
                {
                    m_passedButtons |= ButtonBit(event.button);
                    out.Add(event);
                }
                break;

            case InputEventKind::MouseButtonUp:
                // 넘긴 누름의 뗌만 넘긴다. 넘기지 않은 누름의 뗌은 ImGui 가 모르는 버튼을 뗀다.
                if ((m_passedButtons & ButtonBit(event.button)) != 0)
                {
                    m_passedButtons &= ~ButtonBit(event.button);
                    out.Add(event);
                }
                break;

            case InputEventKind::MouseWheel:
                if (IsPointerAllowed())
                {
                    out.Add(event);
                }
                break;

            case InputEventKind::KeyDown:
                if (event.key == Key::Escape)
                {
                    if (m_keyboardAllowed && m_textInputActive)
                    {
                        out.Add(event);
                        break;
                    }
                    if (false == event.repeat)
                    {
                        m_skipRequested = true;
                    }
                    break;
                }
                if (m_keyboardAllowed)
                {
                    out.Add(event);
                }
                break;

            case InputEventKind::KeyUp:
                // 늘 넘긴다. 막을 켜기 전에 누른 키가 눌린 채로 남으면 안 된다.
                out.Add(event);
                break;

            case InputEventKind::Text:
                if (m_keyboardAllowed)
                {
                    out.Add(event);
                }
                break;

            case InputEventKind::FocusGained:
            case InputEventKind::FocusLost:
                if (event.kind == InputEventKind::FocusLost)
                {
                    ResetPointerState();
                }
                out.Add(event);
                break;

            case InputEventKind::TouchBegan:
                if (event.codePoint < TouchBitCount && IsAllowed(Vector2{ event.x, event.y }))
                {
                    m_passedTouches |= 1u << event.codePoint;
                    out.Add(event);
                }
                break;

            case InputEventKind::TouchMoved:
                if (event.codePoint < TouchBitCount && (m_passedTouches & (1u << event.codePoint)) != 0)
                {
                    out.Add(event);
                }
                break;

            case InputEventKind::TouchEnded:
            case InputEventKind::TouchCancelled:
                if (event.codePoint < TouchBitCount && (m_passedTouches & (1u << event.codePoint)) != 0)
                {
                    m_passedTouches &= ~(1u << event.codePoint);
                    out.Add(event);
                }
                break;
            }
        }
    }

    void EditorGuideFocus::BeginFrame() noexcept
    {
        for (Seen& seen : m_seen)
        {
            seen = Seen{};
        }
        m_popupCount = 0;
        m_balloonSeen = false;
    }

    void EditorGuideFocus::Report(const GuideFocusTarget& target, const Rect& rect, Bool opened, Bool visible, Bool activated,
        Bool enabled, const char* disabledReason, Bool round)
    {
        if (false == m_active)
        {
            return;
        }
        const UInt32 index = m_path.Find(target);
        // 같은 대상이 한 프레임에 두 번 그려지면 먼저 그린 것을 쓴다 - 대상 이름이 겹친 것이고, 앞의 것이 위에 있다.
        if (index >= m_path.count || m_seen[index].seen)
        {
            return;
        }
        Seen& seen = m_seen[index];
        seen.rect = rect;
        seen.seen = true;
        seen.opened = opened;
        seen.visible = visible;
        seen.activated = activated;
        seen.enabled = enabled;
        seen.round = round;
        if (index == m_level && false == enabled)
        {
            std::snprintf(m_disabledReason, sizeof(m_disabledReason), "%s", disabledReason != nullptr ? disabledReason : "");
        }
    }

    Bool EditorGuideFocus::IsCurrentDisabled() const noexcept
    {
        // 그려지지 않은 칸은 `BeginFrame` 이 켜진 것으로 비워 둔다 - 지난 프레임의 회색을 끌고 오지 않는다.
        return m_active && m_level < m_path.count && false == m_seen[m_level].enabled;
    }

    void EditorGuideFocus::ReportPopup(const Rect& rect)
    {
        if (false == m_active || m_popupCount >= PopupCapacity)
        {
            return;
        }
        m_popups[m_popupCount] = rect;
        ++m_popupCount;
    }

    const Rect& EditorGuideFocus::GetPopup(UInt32 index) const
    {
        assert(index < m_popupCount);
        return m_popups[index];
    }

    Bool EditorGuideFocus::IsPopupOpen(UInt32 index) const noexcept
    {
        const Bool atTarget = m_path.count > 0 && m_level + 1 == m_path.count;
        if (false == m_active || false == atTarget || index >= m_popupCount)
        {
            return false;
        }
        const Seen& target = m_seen[m_level];
        if (false == target.seen)
        {
            // 대상을 아직 못 봤다. 품었는지 알 수 없으니 열지 않는다.
            return false;
        }
        return false == m_popups[index].Contains(target.rect.Center());
    }

    void EditorGuideFocus::ReportBalloon(const Rect& rect)
    {
        m_balloon = rect;
        m_balloonSeen = true;
    }

    void EditorGuideFocus::SetPopupBaseline(UInt32 openPopups) noexcept
    {
        m_popupBaseline = openPopups;
        m_popupBaselineSet = true;
    }

    Bool EditorGuideFocus::ShouldOpen(const GuideFocusTarget& target) const noexcept
    {
        if (false == m_active || m_paused)
        {
            return false;
        }
        const UInt32 index = m_path.Find(target);
        if (index >= m_path.count)
        {
            return false;
        }
        // 지나온 칸은 열린 채로 둔다. 닫히면 그 안의 대상이 사라진다.
        return index < m_level || index == m_openRequest;
    }

    Bool EditorGuideFocus::ShouldScrollTo(const GuideFocusTarget& target) const noexcept
    {
        return m_active && false == m_paused && m_path.Find(target) == m_level;
    }

    Bool EditorGuideFocus::IsOpenRequested(const GuideFocusTarget& target) const noexcept
    {
        return m_active && false == m_paused && m_openRequest < m_path.count && m_path.Find(target) == m_openRequest;
    }

    Bool EditorGuideFocus::IsAtTarget() const noexcept
    {
        return m_active && m_path.count > 0 && m_level + 1 == m_path.count && m_holeSettled;
    }

    Bool EditorGuideFocus::ConsumeActivated() noexcept
    {
        const Bool activated = m_activated;
        m_activated = false;
        return activated;
    }

    Float EditorGuideFocus::GetPulse() const noexcept
    {
        return 0.5f + 0.5f * std::sin(m_time * (2.0f * Pi / PulsePeriod));
    }

    void EditorGuideFocus::Update(Float deltaTime)
    {
        const Float dt = deltaTime > 0.0f ? deltaTime : Float(0.0f);
        m_time += dt;

        const Bool shown = m_active && false == m_paused;
        const Float fadeStep = dt / FadeSeconds;
        m_veilAlpha = shown ? std::fmin(1.0f, m_veilAlpha + fadeStep) : std::fmax(0.0f, m_veilAlpha - fadeStep);
        if (false == m_veilAlpha > 0.0f)
        {
            // 다 사라졌다. 다음에 켜면 구멍이 다시 넓은 데서 좁혀 들어온다.
            m_holeKnown = false;
        }
        if (false == shown)
        {
            return;
        }

        // 지나온 칸이 닫혔다(사용자가 메뉴를 닫았다). 그 칸으로 돌아간다 - 그 안의 대상은 이제 그려지지 않는다.
        for (UInt32 index = 0; index < m_level; ++index)
        {
            if (m_seen[index].seen && false == m_seen[index].opened)
            {
                EnterLevel(index);
                break;
            }
        }

        const UInt32 current = m_level;
        const Seen& seen = m_seen[current];
        const Bool leaf = current + 1 == m_path.count;
        Rect allowedTarget;
        Bool hasAllowedTarget = false;
        if (seen.seen)
        {
            m_unseen = 0.0f;
            m_holeTarget = Inflate(seen.rect, HolePadding);
            m_holeRound = seen.round;
            allowedTarget = m_holeTarget;
            hasAllowedTarget = true;
            if (false == m_holeKnown)
            {
                m_hole = Inflate(m_holeTarget, HoleIntroSpread);
                m_holeKnown = true;
            }
            const Float blend = 1.0f - std::exp(-HoleFollowRate * dt);
            m_hole.min.x = Towards(m_hole.min.x, m_holeTarget.min.x, blend);
            m_hole.min.y = Towards(m_hole.min.y, m_holeTarget.min.y, blend);
            m_hole.max.x = Towards(m_hole.max.x, m_holeTarget.max.x, blend);
            m_hole.max.y = Towards(m_hole.max.y, m_holeTarget.max.y, blend);
            if (Farthest(m_hole, m_holeTarget) <= HoleSettleDistance)
            {
                m_hole = m_holeTarget;
                m_holeSettled = true;
            }
            else
            {
                m_holeSettled = false;
            }
            m_settledSeconds = m_holeSettled ? m_settledSeconds + dt : Float(0.0f);

            if (false == leaf)
            {
                if (seen.opened)
                {
                    // 열렸다(스스로 열었거나 사용자가 열었거나 이미 열려 있었다). 안쪽 칸으로 간다.
                    EnterLevel(current + 1);
                }
                else if (m_path.open[current] == GuideFocusOpen::Auto && m_holeSettled)
                {
                    m_dwell += dt;
                    if (m_dwell >= DwellSeconds)
                    {
                        m_openRequest = current;
                    }
                }
            }
            else if (seen.activated)
            {
                m_activated = true;
            }
        }
        else
        {
            m_unseen += dt;
            if (m_unseen >= BrokenSeconds && false == m_broken)
            {
                m_broken = true;
                Log::Write(LogLevel::Warning, "editor",
                    "guide focus: step %u of %u was not drawn for %.1f s; the path is broken",
                    current + 1, m_path.count, static_cast<double>(BrokenSeconds));
            }
        }

        // **다음 프레임에 누를 수 있는 곳**이다. 움직이는 구멍이 아니라 도착할 자리로 잰다 - 옮겨 가는 도중에
        // 구멍이 지나는 자리의 위젯이 눌리면 안 된다.
        ClearAllowedRects();
        if (hasAllowedTarget)
        {
            AddAllowedRect(allowedTarget);
        }
        for (UInt32 index = 0; index < m_popupCount; ++index)
        {
            if (IsPopupOpen(index))
            {
                AddAllowedRect(m_popups[index]);
            }
        }
        if (m_balloonSeen)
        {
            AddAllowedRect(m_balloon);
        }
    }
}
