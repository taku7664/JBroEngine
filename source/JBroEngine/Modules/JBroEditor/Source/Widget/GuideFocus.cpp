#include <JBro/Editor/Widget/GuideFocus.h>

#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/Widget/Basic.h>

#include <imgui_internal.h>

#include <cmath>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::Widget
{
    namespace
    {
        // **가리키는 칸이 창 안에 다 들어와 있는가.** 조금이라도 보이면 된다고 보면, 목록 끝에 반쯤 걸친 칸을 굴리지 않아 구멍이 창 밖에 걸린다 -
        // 컴포넌트 목록이 두 칸 길어지자 가이드 시험이 그렇게 실패했다(D-291). 창보다 큰 칸은 다 들어올 수 없으니 보이기만 하면 된다(매 프레임 굴리지 않는다).
        Bool IsWhollyInWindow(const ImVec2& min, const ImVec2& max)
        {
            const ImRect clip = ImGui::GetCurrentWindow()->InnerClipRect;
            if (max.y - min.y > clip.Max.y - clip.Min.y)
            {
                return ImGui::IsRectVisible(min, max);
            }
            return min.y >= clip.Min.y && max.y <= clip.Max.y;
        }
    }

    namespace
    {
        EditorGuideFocus* g_focus = nullptr;
        GuideFocusTarget g_nextTarget;

        // 막에 뚫는 구멍은 대상 하나와 이 경로가 연 팝업들이다.
        constexpr UInt32 MaxHoles = 1 + EditorGuideFocus::PopupCapacity;
        constexpr UInt32 MaxEdges = 2 + MaxHoles * 2;
        // 말풍선의 폭, 대상과의 틈, 미끄러져 들어오는 거리와 시간이다.
        constexpr Float BalloonWidth = 300.0f;
        constexpr Float BalloonGap = 12.0f;
        constexpr Float BalloonSlide = 18.0f;
        constexpr Float BalloonSlideSeconds = 0.22f;

        Rect ToRect(const ImVec2& min, const ImVec2& max)
        {
            return Rect{ { min.x, min.y }, { max.x, max.y } };
        }

        Float EaseOut(Float t)
        {
            const Float clamped = t < 0.0f ? Float(0.0f) : (t > 1.0f ? Float(1.0f) : t);
            const Float inverse = 1.0f - clamped;
            return 1.0f - inverse * inverse * inverse;
        }

        // 작은 배열을 오름차순으로 세우고 겹친 값을 뺀다. 가장자리가 스무 개 남짓이라 삽입 정렬이면 된다.
        UInt32 SortUnique(Float* values, UInt32 count)
        {
            for (UInt32 i = 1; i < count; ++i)
            {
                const Float value = values[i];
                UInt32 j = i;
                while (j > 0 && values[j - 1] > value)
                {
                    values[j] = values[j - 1];
                    --j;
                }
                values[j] = value;
            }
            UInt32 unique = 0;
            for (UInt32 i = 0; i < count; ++i)
            {
                if (unique == 0 || values[i] != values[unique - 1])
                {
                    values[unique] = values[i];
                    ++unique;
                }
            }
            return unique;
        }

        Float Clamp(Float value, Float low, Float high)
        {
            return value < low ? low : (value > high ? high : value);
        }

        // 화면에서 구멍들을 뺀 나머지를 칠한다. 구멍의 가장자리로 화면을 격자로 나누고, 구멍에 들지 않는 칸을
        // 한 줄씩 이어 붙여 사각형으로 칠한다 - 매 프레임 잡는 것이 없다.
        void FillVeil(ImDrawList& list, const ImVec2& display, const ImRect* holes, UInt32 holeCount, ImU32 color)
        {
            Float xs[MaxEdges];
            Float ys[MaxEdges];
            UInt32 xCount = 0;
            UInt32 yCount = 0;
            xs[xCount++] = 0.0f;
            xs[xCount++] = display.x;
            ys[yCount++] = 0.0f;
            ys[yCount++] = display.y;
            for (UInt32 index = 0; index < holeCount; ++index)
            {
                xs[xCount++] = Clamp(holes[index].Min.x, 0.0f, display.x);
                xs[xCount++] = Clamp(holes[index].Max.x, 0.0f, display.x);
                ys[yCount++] = Clamp(holes[index].Min.y, 0.0f, display.y);
                ys[yCount++] = Clamp(holes[index].Max.y, 0.0f, display.y);
            }
            xCount = SortUnique(xs, xCount);
            yCount = SortUnique(ys, yCount);

            for (UInt32 row = 0; row + 1 < yCount; ++row)
            {
                const Float y0 = ys[row];
                const Float y1 = ys[row + 1];
                const Float cy = (y0 + y1) * 0.5f;
                Float runStart = 0.0f;
                Bool running = false;
                for (UInt32 column = 0; column + 1 < xCount; ++column)
                {
                    const Float x0 = xs[column];
                    const Float x1 = xs[column + 1];
                    const ImVec2 center((x0 + x1) * 0.5f, cy);
                    Bool inHole = false;
                    for (UInt32 index = 0; index < holeCount; ++index)
                    {
                        if (holes[index].Contains(center))
                        {
                            inHole = true;
                            break;
                        }
                    }
                    if (false == inHole && false == running)
                    {
                        runStart = x0;
                        running = true;
                    }
                    else if (inHole && running)
                    {
                        list.AddRectFilled(ImVec2(runStart, y0), ImVec2(x0, y1), color);
                        running = false;
                    }
                }
                if (running)
                {
                    list.AddRectFilled(ImVec2(runStart, y0), ImVec2(xs[xCount - 1], y1), color);
                }
            }
        }

        // **둥근 구멍**이다. `FillVeil` 은 사각형만 뺀다 - 그 사각형 안에서 내접하는 타원 밖의 네 귀퉁이를 막 색으로 다시 칠한다.
        // 귀퉁이마다 꼭짓점에서 호의 점들로 부채를 편다. 호가 꼭짓점 쪽으로 볼록하므로 부채는 귀퉁이를 빈틈없이 덮는다.
        void FillOutsideEllipse(ImDrawList& list, const ImRect& hole, ImU32 color)
        {
            constexpr Int32 SegmentsPerQuarter = 12;
            const ImVec2 center = hole.GetCenter();
            const Float rx = hole.GetWidth() * 0.5f;
            const Float ry = hole.GetHeight() * 0.5f;
            const ImVec2 corners[4] = { hole.Max, ImVec2(hole.Min.x, hole.Max.y), hole.Min, ImVec2(hole.Max.x, hole.Min.y) };
            for (Int32 quarter = 0; quarter < 4; ++quarter)
            {
                const Float start = static_cast<JBro::Float>(quarter) * IM_PI * 0.5f;
                ImVec2 previous(center.x + rx * ImCos(start), center.y + ry * ImSin(start));
                for (Int32 step = 1; step <= SegmentsPerQuarter; ++step)
                {
                    const Float angle = start + (IM_PI * 0.5f) * static_cast<JBro::Float>(step) / static_cast<JBro::Float>(SegmentsPerQuarter);
                    const ImVec2 point(center.x + rx * ImCos(angle), center.y + ry * ImSin(angle));
                    list.AddTriangleFilled(corners[quarter], previous, point, color);
                    previous = point;
                }
            }
        }

        // 말풍선을 놓을 자리다. 대상의 오른쪽·왼쪽·아래·위 가운데 화면 안에 들어가는 첫 자리이고,
        // 어디에도 안 들어가면 남는 자리가 가장 넓은 쪽에 두고 화면 안으로 민다. `slide` 는 들어오는 쪽이다.
        ImVec2 PlaceBalloon(const ImRect& target, const ImVec2& size, const ImVec2& display, ImVec2& slide)
        {
            const Float roomRight = display.x - target.Max.x;
            const Float roomLeft = target.Min.x;
            const Float roomBelow = display.y - target.Max.y;
            const Float roomAbove = target.Min.y;
            ImVec2 position;
            if (roomRight >= size.x + BalloonGap)
            {
                position = ImVec2(target.Max.x + BalloonGap, target.Min.y);
                slide = ImVec2(1.0f, 0.0f);
            }
            else if (roomLeft >= size.x + BalloonGap)
            {
                position = ImVec2(target.Min.x - BalloonGap - size.x, target.Min.y);
                slide = ImVec2(-1.0f, 0.0f);
            }
            else if (roomBelow >= size.y + BalloonGap || roomBelow >= roomAbove)
            {
                position = ImVec2(target.Min.x, target.Max.y + BalloonGap);
                slide = ImVec2(0.0f, 1.0f);
            }
            else
            {
                position = ImVec2(target.Min.x, target.Min.y - BalloonGap - size.y);
                slide = ImVec2(0.0f, -1.0f);
            }
            position.x = Clamp(position.x, 8.0f, std::fmax(8.0f, display.x - size.x - 8.0f));
            position.y = Clamp(position.y, 8.0f, std::fmax(8.0f, display.y - size.y - 8.0f));
            return position;
        }
    }

    void SetGuideFocus(EditorGuideFocus* focus)
    {
        g_focus = focus;
        g_nextTarget = {};
    }

    EditorGuideFocus* GetGuideFocus()
    {
        return g_focus;
    }

    void SetNextItemTarget(const GuideFocusTarget& target)
    {
        g_nextTarget = target;
    }

    namespace Internal
    {
        GuideFocusTarget TakeNextItemTarget()
        {
            const GuideFocusTarget target = g_nextTarget;
            g_nextTarget = {};
            return target;
        }

        void OpenIfGuided(const GuideFocusTarget& target)
        {
            if (target.IsValid() && g_focus != nullptr && g_focus->ShouldOpen(target))
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
        }

        void ReportLastItem(const GuideFocusTarget& target, Bool opened, Bool activated,
            Bool enabled, const char* disabledReason)
        {
            if (false == target.IsValid() || g_focus == nullptr || false == g_focus->IsActive())
            {
                return;
            }
            const Bool visible = ImGui::IsItemVisible();
            if (false == IsWhollyInWindow(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()) && g_focus->ShouldScrollTo(target))
            {
                // 지금 칸이 스크롤 밖이거나 반쯤 걸쳤다. 이 줄이 가운데 오게 굴린다 - 다음 프레임에 다 보인다.
                ImGui::SetScrollHereY(0.5f);
            }
            g_focus->Report(target, ToRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), opened, visible, activated,
                enabled, disabledReason);
        }
    }

    void ReportGuideTarget(const GuideFocusTarget& target, const ImVec2& min, const ImVec2& max,
        Bool opened, Bool activated, Bool round)
    {
        if (false == target.IsValid() || g_focus == nullptr || false == g_focus->IsActive())
        {
            return;
        }
        const Bool visible = ImGui::IsRectVisible(min, max);
        if (false == IsWhollyInWindow(min, max) && g_focus->ShouldScrollTo(target))
        {
            ImGui::SetScrollFromPosY(ImGui::GetCurrentWindow(), min.y - ImGui::GetWindowPos().y, 0.5f);
        }
        g_focus->Report(target, ToRect(min, max), opened, visible, activated, true, nullptr, round);
    }

    GuideFocusAction GuideFocus(EditorGuideFocus& focus, const GuideFocusBalloon& balloon)
    {
        ImGuiContext& context = *ImGui::GetCurrentContext();

        // 이 경로가 켜진 뒤에 열린 팝업을 알린다. 그 안도 누를 수 있어야 메뉴를 따라 들어간다.
        if (focus.IsActive())
        {
            const UInt32 open = static_cast<JBro::UInt32>(context.OpenPopupStack.Size);
            if (focus.NeedsPopupBaseline() || open < focus.GetPopupBaseline())
            {
                // 켤 때 열려 있던 것(가이드를 고른 메뉴)은 곧 닫힌다. 닫혀 줄어들면 기준도 따라 내린다 -
                // 그래야 그 뒤에 연 메뉴가 기준 위에 선다.
                focus.SetPopupBaseline(open);
            }
            for (UInt32 index = focus.GetPopupBaseline(); index < open; ++index)
            {
                const ImGuiWindow* window = context.OpenPopupStack[static_cast<JBro::Int32>(index)].Window;
                if (window != nullptr && window->WasActive && 0 == (window->Flags & ImGuiWindowFlags_Modal))
                {
                    focus.ReportPopup(ToRect(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y)));
                }
            }
        }

        const Float alpha = focus.GetVeilAlpha();
        if (false == alpha > 0.0f || false == focus.HasHole())
        {
            return GuideFocusAction::None;
        }
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const Rect& holeRect = focus.GetHoleRect();
        const ImRect hole(ImVec2(holeRect.min.x, holeRect.min.y), ImVec2(holeRect.max.x, holeRect.max.y));

        // ── 막 ─────────────────────────────────────────────────────
        ImRect holes[MaxHoles];
        UInt32 holeCount = 0;
        holes[holeCount++] = hole;
        // 경로의 중간 칸이 연 메뉴는 뚫지 않는다 - 그 안의 다음 칸만 구멍이고 나머지 항목은 막이 덮는다.
        for (UInt32 index = 0; index < focus.GetPopupCount() && holeCount < MaxHoles; ++index)
        {
            if (focus.IsPopupOpen(index))
            {
                const Rect& popup = focus.GetPopup(index);
                holes[holeCount++] = ImRect(ImVec2(popup.min.x, popup.min.y), ImVec2(popup.max.x, popup.max.y));
            }
        }

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(display);
        {
            StyleScope scope;
            scope.PushVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            scope.PushVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            constexpr ImGuiWindowFlags veilFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs
                | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove;
            if (ImGui::Begin("##guide_focus_veil", nullptr, veilFlags))
            {
                ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
                ImDrawList& list = *ImGui::GetWindowDrawList();
                list.PushClipRect(ImVec2(0.0f, 0.0f), display, false);
                const ImVec4 veil = EditorTheme::GuideVeil;
                const ImU32 veilColor = ImGui::GetColorU32(ImVec4(veil.x, veil.y, veil.z, veil.w * alpha));
                FillVeil(list, display, holes, holeCount, veilColor);
                if (focus.IsHoleRound())
                {
                    FillOutsideEllipse(list, hole, veilColor);
                }
                // 테두리가 숨을 쉰다. 머무는 동안 "여기" 라고 말한다.
                const Float pulse = focus.GetPulse();
                const ImVec4 ring = EditorTheme::GuideRing;
                const ImU32 ringColor = ImGui::GetColorU32(ImVec4(ring.x, ring.y, ring.z, (0.55f + 0.45f * pulse) * alpha));
                if (focus.IsHoleRound())
                {
                    list.AddEllipse(hole.GetCenter(), ImVec2(hole.GetWidth() * 0.5f + 1.0f, hole.GetHeight() * 0.5f + 1.0f), ringColor,
                        0.0f, 0, 1.5f + 1.0f * pulse);
                }
                else
                {
                    list.AddRect(ImVec2(hole.Min.x - 1.0f, hole.Min.y - 1.0f), ImVec2(hole.Max.x + 1.0f, hole.Max.y + 1.0f),
                        ringColor, 4.0f, 0, 1.5f + 1.0f * pulse);
                }
                list.PopClipRect();
            }
            ImGui::End();
        }

        // ── 말풍선 ─────────────────────────────────────────────────
        GuideFocusAction action = GuideFocusAction::None;
        if (false == focus.IsActive() || balloon.title == nullptr)
        {
            return action;
        }
        ImVec2 size(BalloonWidth, 120.0f);
        if (const ImGuiWindow* previous = ImGui::FindWindowByName("##guide_focus_balloon"))
        {
            if (previous->Size.x > 0.0f && previous->Size.y > 0.0f)
            {
                size = previous->Size;
            }
        }
        ImVec2 slide;
        ImVec2 position = PlaceBalloon(hole, size, display, slide);
        const Float arrive = EaseOut(focus.GetSettledSeconds() / BalloonSlideSeconds);
        position.x += slide.x * BalloonSlide * (1.0f - arrive);
        position.y += slide.y * BalloonSlide * (1.0f - arrive);
        ImGui::SetNextWindowPos(position);
        ImGui::SetNextWindowSize(ImVec2(BalloonWidth, 0.0f));
        {
            StyleScope scope;
            scope.PushVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
            scope.PushVar(ImGuiStyleVar_WindowRounding, 6.0f);
            scope.PushVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
            scope.PushVar(ImGuiStyleVar_Alpha, alpha * (0.35f + 0.65f * arrive));
            scope.PushColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
            scope.PushColor(ImGuiCol_Border, EditorTheme::GuideRing);
            constexpr ImGuiWindowFlags balloonFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
                | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::Begin("##guide_focus_balloon", nullptr, balloonFlags))
            {
                ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
                if (balloon.progress != nullptr)
                {
                    HintText(balloon.progress);
                }
                Text(balloon.title);
                if (balloon.body != nullptr)
                {
                    ImGui::Spacing();
                    WrappedText(balloon.body);
                }
                if (balloon.note != nullptr)
                {
                    ImGui::Spacing();
                    HintText(balloon.note);
                }
                ImGui::Spacing();
                Bool first = true;
                const auto nextInRow = [&first]() {
                    if (false == first)
                    {
                        ImGui::SameLine();
                    }
                    first = false;
                };
                if (balloon.skipLabel != nullptr)
                {
                    nextInRow();
                    if (Button(balloon.skipLabel))
                    {
                        action = GuideFocusAction::Skip;
                    }
                }
                if (balloon.backLabel != nullptr)
                {
                    nextInRow();
                    if (ActionButton(balloon.backLabel, Severity::Info, balloon.backEnabled, balloon.backDisabledReason))
                    {
                        action = GuideFocusAction::Back;
                    }
                }
                if (balloon.nextLabel != nullptr)
                {
                    nextInRow();
                    if (ActionButton(balloon.nextLabel, Severity::Success, balloon.nextEnabled, balloon.nextDisabledReason))
                    {
                        action = GuideFocusAction::Next;
                    }
                }
                // 자동 크기는 한 프레임 늦다. 이번 프레임에 그린 끝까지 넣어야 늘어난 줄의 단추가 허용 영역 밖에 걸리지 않는다.
                const ImGuiWindow* window = ImGui::GetCurrentWindow();
                const ImVec2 drawnMax(window->DC.CursorMaxPos.x + window->WindowPadding.x, window->DC.CursorMaxPos.y + window->WindowPadding.y);
                focus.ReportBalloon(ToRect(window->Pos,
                    ImVec2(std::fmax(window->Pos.x + window->Size.x, drawnMax.x), std::fmax(window->Pos.y + window->Size.y, drawnMax.y))));
            }
            ImGui::End();
        }
        return action;
    }
}
