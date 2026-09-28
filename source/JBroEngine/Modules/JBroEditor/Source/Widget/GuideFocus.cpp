#include <JBro/Editor/Widget/GuideFocus.h>

#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/Widget/Basic.h>

#include <imgui_internal.h>

#include <cmath>

namespace JBro::Widget
{
    namespace
    {
        EditorGuideFocus* g_focus = nullptr;
        GuideFocusTarget g_nextTarget;

        // 막에 뚫는 구멍은 대상 하나와 이 경로가 연 팝업들이다.
        constexpr std::uint32_t MaxHoles = 1 + EditorGuideFocus::PopupCapacity;
        constexpr std::uint32_t MaxEdges = 2 + MaxHoles * 2;
        // 말풍선의 폭, 대상과의 틈, 미끄러져 들어오는 거리와 시간이다.
        constexpr float BalloonWidth = 300.0f;
        constexpr float BalloonGap = 12.0f;
        constexpr float BalloonSlide = 18.0f;
        constexpr float BalloonSlideSeconds = 0.22f;

        Rect ToRect(const ImVec2& min, const ImVec2& max)
        {
            return Rect{ { min.x, min.y }, { max.x, max.y } };
        }

        float EaseOut(float t)
        {
            const float clamped = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            const float inverse = 1.0f - clamped;
            return 1.0f - inverse * inverse * inverse;
        }

        // 작은 배열을 오름차순으로 세우고 겹친 값을 뺀다. 가장자리가 스무 개 남짓이라 삽입 정렬이면 된다.
        std::uint32_t SortUnique(float* values, std::uint32_t count)
        {
            for (std::uint32_t i = 1; i < count; ++i)
            {
                const float value = values[i];
                std::uint32_t j = i;
                while (j > 0 && values[j - 1] > value)
                {
                    values[j] = values[j - 1];
                    --j;
                }
                values[j] = value;
            }
            std::uint32_t unique = 0;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                if (unique == 0 || values[i] != values[unique - 1])
                {
                    values[unique] = values[i];
                    ++unique;
                }
            }
            return unique;
        }

        float Clamp(float value, float low, float high)
        {
            return value < low ? low : (value > high ? high : value);
        }

        // 화면에서 구멍들을 뺀 나머지를 칠한다. 구멍의 가장자리로 화면을 격자로 나누고, 구멍에 들지 않는 칸을
        // 한 줄씩 이어 붙여 사각형으로 칠한다 - 매 프레임 잡는 것이 없다.
        void FillVeil(ImDrawList& list, const ImVec2& display, const ImRect* holes, std::uint32_t holeCount, ImU32 color)
        {
            float xs[MaxEdges];
            float ys[MaxEdges];
            std::uint32_t xCount = 0;
            std::uint32_t yCount = 0;
            xs[xCount++] = 0.0f;
            xs[xCount++] = display.x;
            ys[yCount++] = 0.0f;
            ys[yCount++] = display.y;
            for (std::uint32_t index = 0; index < holeCount; ++index)
            {
                xs[xCount++] = Clamp(holes[index].Min.x, 0.0f, display.x);
                xs[xCount++] = Clamp(holes[index].Max.x, 0.0f, display.x);
                ys[yCount++] = Clamp(holes[index].Min.y, 0.0f, display.y);
                ys[yCount++] = Clamp(holes[index].Max.y, 0.0f, display.y);
            }
            xCount = SortUnique(xs, xCount);
            yCount = SortUnique(ys, yCount);

            for (std::uint32_t row = 0; row + 1 < yCount; ++row)
            {
                const float y0 = ys[row];
                const float y1 = ys[row + 1];
                const float cy = (y0 + y1) * 0.5f;
                float runStart = 0.0f;
                bool running = false;
                for (std::uint32_t column = 0; column + 1 < xCount; ++column)
                {
                    const float x0 = xs[column];
                    const float x1 = xs[column + 1];
                    const ImVec2 center((x0 + x1) * 0.5f, cy);
                    bool inHole = false;
                    for (std::uint32_t index = 0; index < holeCount; ++index)
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

        // 말풍선을 놓을 자리다. 대상의 오른쪽·왼쪽·아래·위 가운데 화면 안에 들어가는 첫 자리이고,
        // 어디에도 안 들어가면 남는 자리가 가장 넓은 쪽에 두고 화면 안으로 민다. `slide` 는 들어오는 쪽이다.
        ImVec2 PlaceBalloon(const ImRect& target, const ImVec2& size, const ImVec2& display, ImVec2& slide)
        {
            const float roomRight = display.x - target.Max.x;
            const float roomLeft = target.Min.x;
            const float roomBelow = display.y - target.Max.y;
            const float roomAbove = target.Min.y;
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

        void ReportLastItem(const GuideFocusTarget& target, bool opened, bool activated)
        {
            if (false == target.IsValid() || g_focus == nullptr || false == g_focus->IsActive())
            {
                return;
            }
            const bool visible = ImGui::IsItemVisible();
            if (false == visible && g_focus->ShouldScrollTo(target))
            {
                // 지금 칸이 스크롤 밖이다. 이 줄이 가운데 오게 굴린다 - 다음 프레임에 보인다.
                ImGui::SetScrollHereY(0.5f);
            }
            g_focus->Report(target, ToRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), opened, visible, activated);
        }
    }

    void ReportGuideTarget(const GuideFocusTarget& target, const ImVec2& min, const ImVec2& max,
        bool opened, bool activated)
    {
        if (false == target.IsValid() || g_focus == nullptr || false == g_focus->IsActive())
        {
            return;
        }
        const bool visible = ImGui::IsRectVisible(min, max);
        if (false == visible && g_focus->ShouldScrollTo(target))
        {
            ImGui::SetScrollFromPosY(ImGui::GetCurrentWindow(), min.y - ImGui::GetWindowPos().y, 0.5f);
        }
        g_focus->Report(target, ToRect(min, max), opened, visible, activated);
    }

    GuideFocusAction GuideFocus(EditorGuideFocus& focus, const GuideFocusBalloon& balloon)
    {
        ImGuiContext& context = *ImGui::GetCurrentContext();

        // 이 경로가 켜진 뒤에 열린 팝업을 알린다. 그 안도 누를 수 있어야 메뉴를 따라 들어간다.
        if (focus.IsActive())
        {
            const std::uint32_t open = static_cast<std::uint32_t>(context.OpenPopupStack.Size);
            if (focus.NeedsPopupBaseline() || open < focus.GetPopupBaseline())
            {
                // 켤 때 열려 있던 것(가이드를 고른 메뉴)은 곧 닫힌다. 닫혀 줄어들면 기준도 따라 내린다 -
                // 그래야 그 뒤에 연 메뉴가 기준 위에 선다.
                focus.SetPopupBaseline(open);
            }
            for (std::uint32_t index = focus.GetPopupBaseline(); index < open; ++index)
            {
                const ImGuiWindow* window = context.OpenPopupStack[static_cast<int>(index)].Window;
                if (window != nullptr && window->WasActive && 0 == (window->Flags & ImGuiWindowFlags_Modal))
                {
                    focus.ReportPopup(ToRect(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y)));
                }
            }
        }

        const float alpha = focus.GetVeilAlpha();
        if (false == alpha > 0.0f || false == focus.HasHole())
        {
            return GuideFocusAction::None;
        }
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const Rect& holeRect = focus.GetHoleRect();
        const ImRect hole(ImVec2(holeRect.min.x, holeRect.min.y), ImVec2(holeRect.max.x, holeRect.max.y));

        // ── 막 ─────────────────────────────────────────────────────
        ImRect holes[MaxHoles];
        std::uint32_t holeCount = 0;
        holes[holeCount++] = hole;
        if (focus.IsActive())
        {
            const std::uint32_t open = static_cast<std::uint32_t>(context.OpenPopupStack.Size);
            for (std::uint32_t index = focus.GetPopupBaseline(); index < open && holeCount < MaxHoles; ++index)
            {
                const ImGuiWindow* window = context.OpenPopupStack[static_cast<int>(index)].Window;
                if (window != nullptr && window->WasActive && 0 == (window->Flags & ImGuiWindowFlags_Modal))
                {
                    holes[holeCount++] = window->Rect();
                }
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
                FillVeil(list, display, holes, holeCount,
                    ImGui::GetColorU32(ImVec4(veil.x, veil.y, veil.z, veil.w * alpha)));
                // 테두리가 숨을 쉰다. 머무는 동안 "여기" 라고 말한다.
                const float pulse = focus.GetPulse();
                const ImVec4 ring = EditorTheme::GuideRing;
                list.AddRect(ImVec2(hole.Min.x - 1.0f, hole.Min.y - 1.0f), ImVec2(hole.Max.x + 1.0f, hole.Max.y + 1.0f),
                    ImGui::GetColorU32(ImVec4(ring.x, ring.y, ring.z, (0.55f + 0.45f * pulse) * alpha)),
                    4.0f, 0, 1.5f + 1.0f * pulse);
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
        const float arrive = EaseOut(focus.GetSettledSeconds() / BalloonSlideSeconds);
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
                ImGui::Spacing();
                bool first = true;
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
                    if (ActionButton(balloon.nextLabel, Severity::Success))
                    {
                        action = GuideFocusAction::Next;
                    }
                }
                const ImGuiWindow* window = ImGui::GetCurrentWindow();
                focus.ReportBalloon(ToRect(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y)));
            }
            ImGui::End();
        }
        return action;
    }
}
