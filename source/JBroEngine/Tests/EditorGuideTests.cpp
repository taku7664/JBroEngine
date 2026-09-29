#include <JBro/Core/Log.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Editor/EditorActions.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorGuide.h>
#include <JBro/Editor/EditorGuideFocus.h>
#include <JBro/Editor/EditorPopup.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Runtime/GameObject.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <stdexcept>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// 가이드 포커스 2~5 단계(D-251, `tasks/guide-focus-plan.md` §3): 부모부터 여는 걸음, 막과 애니메이션, 표식, 가이드.
//
// 앞 절은 **ImGui 없이** 모델의 걸음(칸 넘기기·머묾·끊김·허용 영역·막의 짙기·멈춤)을 재고,
// 뒤 절은 실제 에디터(숨긴 창)를 `PostMessageW` 로 몰아 가이드가 화면의 위젯을 따라가는지 잰다.

namespace
{
    void Check(bool condition, const char* message)
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
        bool m_previous = true;
    };

    using JBro::EditorGuideFocus;
    using JBro::GuideFocusOpen;
    using JBro::GuideFocusPath;
    using JBro::GuideFocusTarget;
    using JBro::Rect;

    constexpr float Frame = 1.0f / 60.0f;

    const GuideFocusTarget A{ JBro::MakeNameId("test.a"), 0 };
    const GuideFocusTarget B{ JBro::MakeNameId("test.b"), 0 };
    const GuideFocusTarget C{ JBro::MakeNameId("test.c"), 0 };
    const Rect RectA{ { 10.0f, 10.0f }, { 110.0f, 30.0f } };
    const Rect RectB{ { 20.0f, 40.0f }, { 120.0f, 60.0f } };
    const Rect RectC{ { 30.0f, 70.0f }, { 130.0f, 90.0f } };

    // 칸 A(스스로 연다) → B(사용자가 연다) → C(대상).
    GuideFocusPath ThreeLevels()
    {
        GuideFocusPath path;
        Check(path.Push(A) && path.Push(B, GuideFocusOpen::User) && path.Push(C), "the path must take three levels");
        return path;
    }

    // 그리는 쪽처럼 한 프레임을 돈다. `openA` 등은 그 칸이 열려 있는가이고, `obey` 면 열어 달라는 칸을 연다.
    struct Drawer
    {
        bool openA = false;
        bool openB = false;
        bool drawC = true;
        bool activateC = false;
        bool obey = true;

        void Frame(EditorGuideFocus& focus, float dt = ::Frame)
        {
            focus.BeginFrame();
            if (obey && focus.ShouldOpen(A))
            {
                openA = true;
            }
            focus.Report(A, RectA, openA, true, false);
            if (openA)
            {
                focus.Report(B, RectB, openB, true, false);
                if (openB && drawC)
                {
                    focus.Report(C, RectC, false, true, activateC);
                }
            }
            focus.Update(dt);
        }
    };

    Rect Padded(const Rect& rect)
    {
        const float pad = EditorGuideFocus::HolePadding;
        return Rect{ { rect.min.x - pad, rect.min.y - pad }, { rect.max.x + pad, rect.max.y + pad } };
    }

    bool SameRect(const Rect& a, const Rect& b)
    {
        return a.min.x == b.min.x && a.min.y == b.min.y && a.max.x == b.max.x && a.max.y == b.max.y;
    }

    // ── 걸음 ──────────────────────────────────────────────────────────

    void TestAClosedLevelOpensOnlyAfterTheHoleSettlesAndDwells()
    {
        EditorGuideFocus focus;
        Check(focus.Begin(ThreeLevels()), "the focus must start");
        Drawer drawer;
        drawer.obey = false;
        // 구멍이 넓은 데서 좁혀 오는 동안은 열지 않는다.
        for (int frame = 0; frame < 5; ++frame)
        {
            drawer.Frame(focus);
        }
        Check(false == focus.IsHoleSettled(), "the hole is still closing in from its wide start after five frames");
        Check(false == focus.ShouldOpen(A), "nothing is opened while the hole is still on its way");

        float waited = 0.0f;
        while (false == focus.IsHoleSettled() && waited < 2.0f)
        {
            drawer.Frame(focus);
            waited += Frame;
        }
        Check(focus.IsHoleSettled(), "the hole must settle on the first level");
        Check(SameRect(focus.GetHoleRect(), Padded(RectA)), "settled means exactly on the level, with its padding");
        Check(false == focus.ShouldOpen(A), "settling alone does not open it - the eye needs a moment on it");

        float dwelled = 0.0f;
        while (false == focus.ShouldOpen(A) && dwelled < 2.0f)
        {
            drawer.Frame(focus);
            dwelled += Frame;
        }
        Check(focus.ShouldOpen(A), "after the dwell the level asks to be opened");
        Check(dwelled >= EditorGuideFocus::DwellSeconds - Frame * 1.5f && dwelled <= EditorGuideFocus::DwellSeconds + Frame * 1.5f,
            "and it waited for the dwell, not a frame more or less");
        Check(focus.IsOpenRequested(A), "the request can be read by whoever cannot open with SetNextItemOpen");
        Check(focus.GetLevel() == 0, "asking is not yet opening");

        drawer.obey = true;
        drawer.Frame(focus);
        Check(focus.GetLevel() == 1, "once the drawer opened it, the walk goes one level in");
        Check(false == focus.IsOpenRequested(A), "and the request is spent");
        Check(focus.ShouldOpen(A), "a level already walked through is kept open");
    }

    void TestALevelTheUserOpensWaitsForTheUser()
    {
        EditorGuideFocus focus;
        Check(focus.Begin(ThreeLevels()), "the focus must start");
        Drawer drawer;
        drawer.openA = true;
        drawer.Frame(focus);
        Check(focus.GetLevel() == 1, "an already open level is walked through without dwelling");
        for (int frame = 0; frame < 180; ++frame)
        {
            drawer.Frame(focus);
        }
        Check(focus.GetLevel() == 1, "a menu the user opens is not opened for them, however long they wait");
        Check(false == focus.ShouldOpen(B) && false == focus.IsOpenRequested(B), "and nothing asks to open it");

        drawer.openB = true;
        drawer.Frame(focus);
        Check(focus.GetLevel() == 2, "when the user opens it the walk goes on to the target");
        for (int frame = 0; frame < 60; ++frame)
        {
            drawer.Frame(focus);
        }
        Check(focus.IsAtTarget(), "and settles on the target");
        Check(false == focus.ConsumeActivated(), "the target was not pressed yet");
        drawer.activateC = true;
        drawer.Frame(focus);
        Check(focus.ConsumeActivated(), "pressing the target is reported once");
        drawer.activateC = false;
        drawer.Frame(focus);
        Check(false == focus.ConsumeActivated(), "and only once");
    }

    void TestClosingAWalkedLevelGoesBackToIt()
    {
        EditorGuideFocus focus;
        Check(focus.Begin(ThreeLevels()), "the focus must start");
        Drawer drawer;
        drawer.openA = true;
        drawer.openB = true;
        drawer.Frame(focus);
        drawer.Frame(focus);
        Check(focus.GetLevel() == 2, "both open levels are walked through");
        // 사용자가 메뉴를 닫았다.
        drawer.openB = false;
        drawer.Frame(focus);
        Check(focus.GetLevel() == 1, "closing a walked level brings the walk back to it - its inside is gone");
    }

    void TestALevelThatIsNeverDrawnBreaksThePath()
    {
        QuietLog quiet;
        EditorGuideFocus focus;
        Check(focus.Begin(ThreeLevels()), "the focus must start");
        Drawer drawer;
        drawer.openA = true;
        drawer.openB = true;
        drawer.drawC = false;
        float waited = 0.0f;
        while (waited < EditorGuideFocus::BrokenSeconds - 0.1f)
        {
            drawer.Frame(focus);
            waited += Frame;
        }
        Check(false == focus.IsBroken(), "a target missing for less than the limit is not broken yet");
        for (int frame = 0; frame < 10; ++frame)
        {
            drawer.Frame(focus);
        }
        Check(focus.IsBroken(), "a target not drawn for the limit breaks the path");
        drawer.drawC = true;
        drawer.openB = false;
        drawer.Frame(focus);
        Check(false == focus.IsBroken(), "going back to another level starts counting again");
    }

    void TestTheAllowedAreaIsWhereTheHoleIsGoingNotWhereItIs()
    {
        EditorGuideFocus focus;
        Check(focus.Begin(ThreeLevels()), "the focus must start");
        focus.BeginFrame();
        focus.Report(A, RectA, false, true, false);
        const Rect popup{ { 300.0f, 300.0f }, { 400.0f, 400.0f } };
        const Rect balloon{ { 500.0f, 10.0f }, { 700.0f, 110.0f } };
        focus.ReportPopup(popup);
        focus.ReportBalloon(balloon);
        focus.Update(Frame);
        Check(false == focus.IsHoleSettled(), "the hole has only begun to move");
        Check(focus.GetAllowedRectCount() == 2, "the target and the balloon are allowed");
        Check(SameRect(focus.GetAllowedRect(0), Padded(RectA)), "the target is allowed where it is, with its padding");
        const Rect& hole = focus.GetHoleRect();
        Check(hole.min.x < RectA.min.x - 50.0f, "the moving hole is still far wider than the target");
        Check(false == focus.IsAllowed({ hole.min.x + 5.0f, hole.min.y + 5.0f }),
            "and what the moving hole passes over is not pressable");
        Check(focus.IsAllowed({ 600.0f, 50.0f }), "inside the balloon is");
        Check(false == focus.IsAllowed({ 350.0f, 350.0f }) && false == focus.IsPopupOpen(0),
            "a popup open while the walk is still on a level along the path is not - only the next level inside it will be");

        // 다음 프레임에 보고가 없으면 대상도 없다. 팝업과 말풍선도 이번 프레임 것만 산다.
        focus.BeginFrame();
        focus.Update(Frame);
        Check(focus.GetAllowedRectCount() == 0, "what was not reported this frame is not allowed next frame");
    }

    // 경로 끝에 닿은 뒤에 열린 팝업(콤보의 목록)만 통째로 열린다. 앞 칸이 연 메뉴는 닫힌 채로 덮인다.
    void TestOnlyPopupsTheTargetOpensAreOpenToTheUser()
    {
        EditorGuideFocus focus;
        GuideFocusPath path;
        Check(path.Push(A, GuideFocusOpen::User) && path.Push(B), "the path must take a menu and its item");
        Check(focus.Begin(path), "the focus must start");
        const Rect menu{ { 10.0f, 30.0f }, { 150.0f, 200.0f } };
        const Rect list{ { 300.0f, 300.0f }, { 400.0f, 400.0f } };

        // 사용자가 메뉴를 열었다. 메뉴의 팝업이 떴고 그 안에 B 가 있다.
        focus.BeginFrame();
        focus.Report(A, RectA, true, true, false);
        focus.ReportPopup(menu);
        focus.Update(Frame);
        Check(focus.GetLevel() == 1, "opening the menu walks on to its item");
        focus.BeginFrame();
        focus.Report(A, RectA, true, true, false);
        focus.Report(B, RectB, false, true, false);
        focus.ReportPopup(menu);
        focus.Update(Frame);
        Check(false == focus.IsPopupOpen(0), "the menu the path opened is not opened whole");
        Check(focus.IsAllowed({ 70.0f, 50.0f }), "its next level, the item, is pressable");
        Check(false == focus.IsAllowed({ 60.0f, 150.0f }), "but not the other items of the menu");

        // 대상 B 가 목록을 열었다(콤보). 그 목록은 통째로 열린다.
        focus.BeginFrame();
        focus.Report(A, RectA, true, true, false);
        focus.Report(B, RectB, false, true, false);
        focus.ReportPopup(menu);
        focus.ReportPopup(list);
        focus.Update(Frame);
        Check(focus.IsPopupOpen(1), "a popup the target opened is open to the user");
        Check(focus.IsAllowed({ 350.0f, 350.0f }), "picking inside it is the step's work");
        Check(false == focus.IsPopupOpen(0), "while the menu under it stays covered");
    }

    void TestReportsOutsideThePathOrTwiceAreIgnored()
    {
        EditorGuideFocus focus;
        focus.Report(A, RectA, true, true, false);
        Check(false == focus.IsActive(), "a report with the veil down changes nothing");
        Check(focus.Begin(ThreeLevels()), "the focus must start");
        focus.BeginFrame();
        focus.Report({ JBro::MakeNameId("test.elsewhere"), 0 }, RectB, true, true, false);
        focus.Report(A, RectA, false, true, false);
        // 같은 이름이 한 프레임에 또 그려졌다. 앞의 것이 쓰인다.
        focus.Report(A, RectC, true, true, false);
        focus.Update(Frame);
        Check(focus.GetLevel() == 0, "the second drawing of the same target does not count as opened");
        Check(SameRect(focus.GetAllowedRect(0), Padded(RectA)), "the first drawing's place is the one used");
    }

    void TestTheVeilFadesInAndOut()
    {
        EditorGuideFocus focus;
        Check(focus.GetVeilAlpha() == 0.0f, "no veil before it starts");
        Check(focus.Begin(ThreeLevels()), "the focus must start");
        Drawer drawer;
        drawer.Frame(focus, EditorGuideFocus::FadeSeconds * 0.5f);
        Check(std::fabs(focus.GetVeilAlpha() - 0.5f) < 0.01f, "half the fade time is half the veil");
        drawer.Frame(focus, EditorGuideFocus::FadeSeconds);
        Check(focus.GetVeilAlpha() == 1.0f, "and it tops out");
        focus.End();
        focus.BeginFrame();
        focus.Update(EditorGuideFocus::FadeSeconds * 0.5f);
        Check(std::fabs(focus.GetVeilAlpha() - 0.5f) < 0.01f, "ending fades it out the same way");
        Check(focus.HasHole(), "and the hole stays cut while it fades");
        focus.Update(EditorGuideFocus::FadeSeconds);
        Check(focus.GetVeilAlpha() == 0.0f && false == focus.HasHole(), "until it is gone, hole and all");
    }

    void TestPausingLiftsTheVeilAndOpensTheGate()
    {
        EditorGuideFocus focus;
        focus.SetPaused(true);
        Check(false == focus.IsPaused(), "nothing to pause while the veil is down");
        Check(focus.Begin(ThreeLevels()), "the focus must start");
        Drawer drawer;
        drawer.obey = false;
        for (int frame = 0; frame < 120; ++frame)
        {
            drawer.Frame(focus);
        }
        Check(focus.ShouldOpen(A), "the first level is asking to be opened");
        focus.SetPaused(true);
        Check(false == focus.ShouldOpen(A), "a paused walk asks for nothing - the modal is the user's business");

        JBro::InputEvent down;
        down.kind = JBro::InputEventKind::MouseButtonDown;
        JBro::InputEvent move;
        move.kind = JBro::InputEventKind::MouseMove;
        move.x = 900.0f;
        move.y = 700.0f;
        const JBro::InputEvent events[] = { move, down };
        JBro::Array<JBro::InputEvent> out;
        focus.FilterInput({ events, 2 }, out);
        Check(out.Size() == 2 && out[0].x == 900.0f, "a paused gate lets a press anywhere through, to answer the modal");

        for (int frame = 0; frame < 30; ++frame)
        {
            drawer.Frame(focus);
        }
        Check(focus.GetVeilAlpha() == 0.0f, "and the veil is lifted");
        Check(focus.IsActive() && focus.GetLevel() == 0, "but the walk is kept where it was");
        focus.SetPaused(false);
        drawer.Frame(focus);
        Check(focus.GetVeilAlpha() > 0.0f, "resuming brings the veil back");
    }

    // ── 실제 에디터 ───────────────────────────────────────────────────

    HWND FindOwnEditorWindow()
    {
        struct Search
        {
            HWND found = nullptr;
            DWORD process = GetCurrentProcessId();
        } search;
        EnumWindows(
            [](HWND hwnd, LPARAM param) -> BOOL {
                Search& search = *reinterpret_cast<Search*>(param);
                DWORD owner = 0;
                GetWindowThreadProcessId(hwnd, &owner);
                if (owner != search.process)
                {
                    return TRUE;
                }
                wchar_t className[64] = {};
                wchar_t title[64] = {};
                GetClassNameW(hwnd, className, 64);
                GetWindowTextW(hwnd, title, 64);
                if (std::wcscmp(className, L"JBroEngineWindow") == 0 && std::wcscmp(title, L"JBro Editor") == 0)
                {
                    search.found = hwnd;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        return search.found;
    }

    bool OpenEditor(JBro::EditorApplication& editor, const char* projectName, HWND& hwnd)
    {
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            return false;
        }
        JBro::ProjectDescriptor project;
        project.name = { projectName, static_cast<std::uint32_t>(std::strlen(projectName)) };
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({ 64, 48 }), "the editor UI must turn on");
        hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be found");
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }
        return true;
    }

    void Tick(JBro::EditorApplication& editor, int frames)
    {
        for (int frame = 0; frame < frames; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick");
        }
    }

    void ClickAt(JBro::EditorApplication& editor, HWND hwnd, int x, int y)
    {
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
        Tick(editor, 1);
        PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        Tick(editor, 1);
        PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        Tick(editor, 1);
    }

    // 구멍이 지금 칸에 닿을 때까지 돈다.
    bool WaitUntilSettled(JBro::EditorApplication& editor, std::uint32_t level)
    {
        for (int frame = 0; frame < 240; ++frame)
        {
            const JBro::EditorGuideFocus& focus = editor.GetGuideFocus();
            if (focus.GetLevel() == level && focus.IsHoleSettled())
            {
                return true;
            }
            Tick(editor, 1);
        }
        return false;
    }

    void HoleCenter(const JBro::EditorGuideFocus& focus, int& x, int& y)
    {
        const Rect& hole = focus.GetHoleRect();
        x = static_cast<int>((hole.min.x + hole.max.x) * 0.5f);
        y = static_cast<int>((hole.min.y + hole.max.y) * 0.5f);
    }

    // 말풍선의 단추 줄의 높이를 찾는다. 줄의 맨 왼쪽에는 늘 단추가 있다 - 그 자리를 아래에서 위로 훑어 처음 걸리는 높이다.
    // 창의 크기에서 셈하지 않는다: 자동 크기는 한 프레임 늦어 내용이 바뀐 프레임에는 단추가 창 크기 밖에 있다.
    bool FindBalloonButtonRow(JBro::EditorApplication& editor, HWND hwnd, const ImGuiWindow*& balloon, float& y)
    {
        // 말풍선은 구멍이 자리 잡은 뒤 옆에서 미끄러져 들어온다(0.22 초). 멈춘 뒤에 잰다.
        Tick(editor, 20);
        balloon = ImGui::FindWindowByName("##guide_focus_balloon");
        if (balloon == nullptr || false == balloon->WasActive)
        {
            return false;
        }
        const float x = balloon->Pos.x + 24.0f;
        for (float probe = balloon->Pos.y + balloon->Size.y + 40.0f; probe > balloon->Pos.y; probe -= 4.0f)
        {
            PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(static_cast<int>(x), static_cast<int>(probe)));
            Tick(editor, 1);
            if (ImGui::GetCurrentContext()->HoveredId != 0)
            {
                y = probe - 6.0f;
                return true;
            }
        }
        return false;
    }

    // 단추 줄을 오른쪽 끝부터 훑어, 누를 수 있는 단추 가운데 오른쪽에서 `skip` 개를 지난 것을 누른다
    // (0 이면 맨 오른쪽 - 다음이 있으면 다음이다). 회색 단추는 세지 않는다.
    bool ClickBalloonButtonFromRight(JBro::EditorApplication& editor, HWND hwnd, int skip)
    {
        const ImGuiWindow* balloon = nullptr;
        float y = 0.0f;
        if (false == FindBalloonButtonRow(editor, hwnd, balloon, y))
        {
            return false;
        }
        ImGuiID last = 0;
        int seen = -1;
        for (float x = balloon->Pos.x + balloon->Size.x - 8.0f; x > balloon->Pos.x; x -= 3.0f)
        {
            PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(static_cast<int>(x), static_cast<int>(y)));
            Tick(editor, 1);
            const ImGuiContext& context = *ImGui::GetCurrentContext();
            if (context.HoveredId == 0 || context.HoveredIdIsDisabled || context.HoveredId == last)
            {
                continue;
            }
            last = context.HoveredId;
            ++seen;
            if (seen == skip)
            {
                PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(static_cast<int>(x), static_cast<int>(y)));
                Tick(editor, 1);
                PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(static_cast<int>(x), static_cast<int>(y)));
                Tick(editor, 1);
                return true;
            }
        }
        return false;
    }

    // 단추 줄을 오른쪽 끝부터 훑어 처음 걸리는 단추가 회색인지 본다(다음이 막혔는가).
    bool RightmostBalloonButtonIsDisabled(JBro::EditorApplication& editor, HWND hwnd)
    {
        const ImGuiWindow* balloon = nullptr;
        float y = 0.0f;
        if (false == FindBalloonButtonRow(editor, hwnd, balloon, y))
        {
            return false;
        }
        for (float x = balloon->Pos.x + balloon->Size.x - 8.0f; x > balloon->Pos.x; x -= 3.0f)
        {
            PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(static_cast<int>(x), static_cast<int>(y)));
            Tick(editor, 1);
            const ImGuiContext& context = *ImGui::GetCurrentContext();
            if (context.HoveredId != 0)
            {
                return context.HoveredIdIsDisabled;
            }
        }
        return false;
    }

    bool ClickBalloonRightmostButton(JBro::EditorApplication& editor, HWND hwnd)
    {
        return ClickBalloonButtonFromRight(editor, hwnd, 0);
    }

    // 인스펙터의 슬롯 0 컴포넌트 머리가 접혀 있는지(ImGui 가 창의 상태 저장소에 적은 값).
    ImGuiID ComponentHeaderId(const char* typeName)
    {
        ImGuiWindow* window = ImGui::FindWindowByName("Inspector");
        Check(window != nullptr, "the inspector must have a window");
        int slot = 0;
        const ImGuiID pushed = ImHashData(&slot, sizeof(slot), window->ID);
        return ImHashStr(typeName, 0, pushed);
    }

    void TestTheAddComponentGuideWalksTheInspector()
    {
        JBro::EditorApplication editor;
        HWND hwnd = nullptr;
        if (false == OpenEditor(editor, "GuideAddComponentProbe", hwnd))
        {
            std::cout << "  [skip] no D3D12 device; the add component guide not verified" << std::endl;
            return;
        }
        JBro::GameObject* object = JBro::EditorActions::CreateObject(editor, nullptr);
        Check(object != nullptr && editor.GetSelectedObject() == object, "the new object must be made and picked");
        Check(object->GetComponents().Size() == 1, "a new object starts with its transform only, or the last step proves nothing");
        Tick(editor, 3);

        // 머리를 접어 둔다. 가이드가 부모부터 열어야 필드에 닿는다.
        ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
        const ImGuiID header = ComponentHeaderId("Transform2D");
        inspector->StateStorage.SetInt(header, 0);
        Tick(editor, 2);
        Check(0 == inspector->StateStorage.GetInt(header, 1), "the transform header must be folded for the check");
        // 인스펙터 창도 닫아 둔다. 경로의 첫 칸이 닫힌 패널이면 가이드가 연다.
        JBro::EditorPanel* inspectorPanel = editor.FindPanel("Inspector");
        Check(inspectorPanel != nullptr, "the inspector panel must exist");
        inspectorPanel->SetOpen(false);
        Tick(editor, 2);

        Check(editor.StartGuide("guide.add_component"), "the guide must start");
        Check(editor.GetGuide().IsRunning(), "and run");
        Tick(editor, 10);
        // 오브젝트를 이미 골라 두었다. 조건은 "들어설 때와 달라졌는가" 라 저절로 지나가지 않고, 다음으로 넘긴다.
        Check(editor.GetGuide().GetStepIndex() == 0, "an object picked before the guide does not skip the first step unseen");
        Check(editor.GetGuide().ShowsNext(), "the first step offers Next for one already picked");
        editor.GetGuide().Update(editor, editor.GetGuideFocus(), JBro::GuideFocusAction::Next);
        Check(editor.GetGuide().GetStepIndex() == 1, "Next goes on to the field");
        Tick(editor, 3);
        Check(0 == inspector->StateStorage.GetInt(header, 1), "the header is not flung open before the hole reaches it");
        Check(inspectorPanel->IsOpen(), "the closed inspector the path starts from was opened");
        Check(editor.GetGuideFocus().IsKeyboardAllowed(), "the step that changes a value lets the keyboard through");
        Check(WaitUntilSettled(editor, 2), "the walk must open the header and reach the first field");
        Check(1 == inspector->StateStorage.GetInt(header, 0), "the folded header was opened on the way");

        // 막 밖을 누른다 - 캔버스 뷰 한가운데. 아무 일도 일어나지 않는다.
        const std::size_t undo = editor.GetCommands().GetUndoCount();
        ClickAt(editor, hwnd, 512, 300);
        Check(editor.GetSelectedObject() == object, "a click on the veil does not change the selection");
        Check(editor.GetCommands().GetUndoCount() == undo, "nor edit anything");
        Check(editor.GetGuide().GetStepIndex() == 1, "and the step waits for Next");

        // 값 칸을 두 번 눌러 글자 입력으로 바꾸고 Esc 를 누른다. 편집 취소이지 가이드 끝내기가 아니다.
        {
            const Rect& field = editor.GetGuideFocus().GetHoleRect();
            const int fx = static_cast<int>(field.max.x - 40.0f);
            const int fy = static_cast<int>((field.min.y + field.max.y) * 0.5f);
            PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(fx, fy));
            Tick(editor, 1);
            for (int press = 0; press < 2; ++press)
            {
                PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(fx, fy));
                Tick(editor, 1);
                PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(fx, fy));
                Tick(editor, 1);
            }
            Tick(editor, 1);
            Check(ImGui::GetIO().WantTextInput, "a double click turns the value into a text field");
            PostMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
            Tick(editor, 1);
            PostMessageW(hwnd, WM_KEYUP, VK_ESCAPE, static_cast<LPARAM>(0xC0000001u));
            Tick(editor, 2);
            Check(editor.GetGuide().IsRunning() && editor.GetGuide().GetStepIndex() == 1,
                "Esc while typing cancels the edit and leaves the guide running");
            Check(false == ImGui::GetIO().WantTextInput, "and it did reach the field");
        }
        Check(ClickBalloonRightmostButton(editor, hwnd), "the balloon's Next button must be found and pressed");
        Check(editor.GetGuide().GetStepIndex() == 2, "Next goes on to adding a component");
        Check(editor.GetGuideFocus().IsKeyboardAllowed(), "the add step lets the keyboard through - the list has a search box");
        Check(WaitUntilSettled(editor, 1), "the hole must reach the add component box");

        int x = 0;
        int y = 0;
        HoleCenter(editor.GetGuideFocus(), x, y);
        ClickAt(editor, hwnd, x, y);
        Tick(editor, 2);
        const ImGuiWindow* combo = nullptr;
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
        {
            if (window->WasActive && (window->Flags & ImGuiWindowFlags_Popup) != 0 && std::strstr(window->Name, "##Combo_") != nullptr)
            {
                combo = window;
            }
        }
        Check(combo != nullptr, "pressing the box inside the hole opens its list");
        Check(editor.GetGuideFocus().IsAllowed({ combo->Pos.x + combo->Size.x * 0.5f, combo->Pos.y + combo->Size.y * 0.5f }),
            "and the list the path opened can be pressed too");

        // 목록에서 고르는 대신 같은 편집을 부른다 - 이 단계가 기다리는 것은 "컴포넌트가 붙었다" 이다.
        JBro::EditorActions::AddComponentList list;
        JBro::EditorActions::BuildAddComponentList(*object, list);
        std::size_t pick = list.typeNames.Size();
        for (std::size_t index = 0; index < list.typeNames.Size(); ++index)
        {
            if (list.addable[index])
            {
                pick = index;
                break;
            }
        }
        Check(pick < list.typeNames.Size(), "some component must be addable");
        Check(JBro::EditorActions::AddComponent(editor, *object, list.typeNames[pick]), "the component must attach");
        Tick(editor, 3);
        Check(editor.GetGuide().IsRunning() && editor.GetGuide().IsConfirming(),
            "the last step, once done, waits for OK instead of closing under the user's eyes");
        Check(false == editor.GetGuide().ShowsSkip() && editor.GetGuide().ShowsNext() && editor.GetGuide().ShowsBack(),
            "with Back and OK and no Skip");
        Tick(editor, 30);
        Check(editor.GetGuide().IsConfirming(), "and keeps waiting");
        // 목록 밖(말풍선의 빈 곳)을 눌러 목록을 닫고, 확인을 누른다.
        {
            const ImGuiWindow* balloon = ImGui::FindWindowByName("##guide_focus_balloon");
            Check(balloon != nullptr, "the balloon must be up");
            ClickAt(editor, hwnd, static_cast<int>(balloon->Pos.x + 20.0f), static_cast<int>(balloon->Pos.y + 14.0f));
            Tick(editor, 2);
        }
        Check(ClickBalloonRightmostButton(editor, hwnd), "the balloon's OK button must be found and pressed");
        Check(false == editor.GetGuide().IsRunning(), "OK ends the guide");
        const char* last = editor.GetNotifications().GetLastTitle();
        Check(last != nullptr && std::strcmp(last, JBro::Loc::TextOr(JBro::LocKeys::GuideFinished, "Guide finished")) == 0,
            "and says so in a notification");
        Check(false == editor.GetGuideFocus().IsActive(), "and takes the veil with it");
        Tick(editor, 20);
        Check(editor.GetGuideFocus().GetVeilAlpha() == 0.0f, "which fades away");
    }

    void TestTheBuildGuideWaitsForTheUserToOpenTheMenu()
    {
        JBro::EditorApplication editor;
        HWND hwnd = nullptr;
        if (false == OpenEditor(editor, "GuideBuildProbe", hwnd))
        {
            std::cout << "  [skip] no D3D12 device; the build guide not verified" << std::endl;
            return;
        }
        Check(editor.StartGuide("guide.build_game"), "the guide must start");
        Check(WaitUntilSettled(editor, 0), "the hole must reach the File menu");
        Check(false == editor.GetGuideFocus().IsKeyboardAllowed(), "a step that only clicks holds the keyboard back");
        Tick(editor, 60);
        Check(editor.GetGuideFocus().GetLevel() == 0, "a menu is never opened for the user");
        Check(ImGui::GetCurrentContext()->OpenPopupStack.Size == 0, "no menu popup is open");

        int x = 0;
        int y = 0;
        HoleCenter(editor.GetGuideFocus(), x, y);
        ClickAt(editor, hwnd, x, y);
        Tick(editor, 2);
        Check(ImGui::GetCurrentContext()->OpenPopupStack.Size == 1, "pressing the menu inside the hole opens it");
        Check(WaitUntilSettled(editor, 1), "and the hole moves into the menu to Build Game");
        const ImGuiWindow* menu = ImGui::GetCurrentContext()->OpenPopupStack[0].Window;
        Check(menu != nullptr, "the open menu has a window");
        const Rect& hole = editor.GetGuideFocus().GetHoleRect();
        Check(hole.min.y >= menu->Pos.y - EditorGuideFocus::HolePadding - 1.0f
                && hole.max.y <= menu->Pos.y + menu->Size.y + EditorGuideFocus::HolePadding + 1.0f,
            "the hole sits inside the open menu");
        // 메뉴의 첫 항목(새 프로젝트)은 구멍 밖이다. 눌리지 않아야 한다.
        const float firstItemY = menu->Pos.y + ImGui::GetStyle().WindowPadding.y + ImGui::GetFrameHeight() * 0.5f;
        const float menuX = menu->Pos.x + menu->Size.x * 0.5f;
        Check(firstItemY < hole.min.y, "the first item of the menu is above the hole, or the next check proves nothing");
        Check(false == editor.GetGuideFocus().IsAllowed({ menuX, firstItemY }),
            "the other items of the menu the user opened are not pressable");
        ClickAt(editor, hwnd, static_cast<int>(menuX), static_cast<int>(firstItemY));
        Tick(editor, 2);
        Check(ImGui::GetCurrentContext()->OpenPopupStack.Size == 1, "pressing another item does nothing - the menu is still open");
        Check(editor.GetGuide().IsRunning() && editor.GetGuideFocus().GetLevel() == 1, "and the guide is where it was");

        PostMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
        Tick(editor, 1);
        PostMessageW(hwnd, WM_KEYUP, VK_ESCAPE, static_cast<LPARAM>(0xC0000001u));
        Tick(editor, 1);
        Check(false == editor.GetGuide().IsRunning() && false == editor.GetGuideFocus().IsActive(), "Esc stops the guide");
    }

    void TestAPathToAChildObjectWalksTheHierarchy()
    {
        JBro::EditorApplication editor;
        HWND hwnd = nullptr;
        if (false == OpenEditor(editor, "GuideHierarchyProbe", hwnd))
        {
            std::cout << "  [skip] no D3D12 device; the hierarchy path not verified" << std::endl;
            return;
        }
        JBro::GameObject* parent = JBro::EditorActions::CreateObject(editor, nullptr);
        JBro::GameObject* child = JBro::EditorActions::CreateObject(editor, parent);
        Check(parent != nullptr && child != nullptr && child->GetParent() == parent, "a parent and its child must be made");
        Tick(editor, 3);

        GuideFocusPath path;
        Check(JBro::EditorGuides::AppendObjectPath(editor, *child, path), "the path to the child must be built");
        Check(path.count == 4, "the layers window, the layer, the parent and the child");
        Check(editor.GetGuideFocus().Begin(path), "the guide focus must start");
        Check(WaitUntilSettled(editor, 3), "the walk must reach the child's row");
        const Rect& hole = editor.GetGuideFocus().GetHoleRect();
        const ImGuiWindow* hierarchy = ImGui::FindWindowByName("Hierarchy");
        Check(hierarchy != nullptr && hole.min.x >= hierarchy->Pos.x - 8.0f && hole.max.x <= hierarchy->Pos.x + hierarchy->Size.x + 8.0f,
            "the hole is on a row of the layers window");
        Check(hole.Height() < 40.0f, "one row, not the whole window");
        // 경로가 이 캔버스의 오브젝트 번호를 가리킨다. 프로젝트를 닫으면 꺼진다.
        editor.CloseProject();
        Check(false == editor.GetGuideFocus().IsActive(), "closing the project lowers the veil");
    }

    void TestABrokenStepIsSkipped()
    {
        QuietLog quiet;
        JBro::EditorApplication editor;
        HWND hwnd = nullptr;
        if (false == OpenEditor(editor, "GuideBrokenProbe", hwnd))
        {
            std::cout << "  [skip] no D3D12 device; broken guide steps not verified" << std::endl;
            return;
        }
        // 되돌아갈 단계를 두지 않은 가이드다. 두 단계 모두 없는 것을 가리킨다.
        JBro::Guide guide;
        guide.id = "test.broken";
        for (int index = 0; index < 2; ++index)
        {
            JBro::GuideStep step;
            step.path.Push(JBro::GuideFocusTargets::Panel("Inspector"));
            step.path.Push(JBro::GuideFocusTargets::Menu("test.nowhere"));
            guide.steps.Add(std::move(step));
        }
        Check(editor.GetGuide().Start(guide, editor, editor.GetGuideFocus()), "the guide must start");
        Tick(editor, 90);
        Check(false == editor.GetGuide().IsRunning(), "steps whose target is gone are skipped to the end, not waited on forever");
        Check(false == editor.GetGuideFocus().IsActive(), "and the veil goes with them");
        const char* last = editor.GetNotifications().GetLastTitle();
        Check(last == nullptr || std::strcmp(last, JBro::Loc::TextOr(JBro::LocKeys::GuideFinished, "Guide finished")) != 0,
            "a guide that ran out of targets is not announced as finished");
    }

    // 값 바꾸기 도중에 오브젝트를 잃으면 고르는 단계로 돌아간다. 말없이 끝나지 않는다.
    void TestLosingTheObjectGoesBackToPicking()
    {
        QuietLog quiet;
        JBro::EditorApplication editor;
        HWND hwnd = nullptr;
        if (false == OpenEditor(editor, "GuideRetreatProbe", hwnd))
        {
            std::cout << "  [skip] no D3D12 device; retreating steps not verified" << std::endl;
            return;
        }
        JBro::GameObject* object = JBro::EditorActions::CreateObject(editor, nullptr);
        Check(object != nullptr, "the object must be made");
        Tick(editor, 3);
        Check(editor.StartGuide("guide.add_component"), "the guide must start");
        editor.GetGuide().Update(editor, editor.GetGuideFocus(), JBro::GuideFocusAction::Next);
        Check(WaitUntilSettled(editor, 2), "the walk must reach the field");

        // 선택을 비웠다. 다음은 막히고, 필드가 사라져 끊기면 고르는 단계로 돌아간다.
        editor.ClearSelection();
        Tick(editor, 1);
        Check(editor.GetGuide().WhyNextBlocked(editor) != nullptr, "with nothing picked Next is blocked on the value step");
        editor.GetGuide().Update(editor, editor.GetGuideFocus(), JBro::GuideFocusAction::Next);
        Check(editor.GetGuide().GetStepIndex() == 1, "and a blocked Next does not move");
        Tick(editor, 60);
        Check(editor.GetGuide().IsRunning() && editor.GetGuide().GetStepIndex() == 0,
            "losing the object sends the guide back to picking one, not quietly to its end");
        Check(false == editor.GetGuide().IsRevisiting(), "as a fresh step that a pick passes");

        // 다시 고르면 넘어간다. 이번에는 오브젝트를 지운다.
        editor.SetSelectedObject(object);
        Tick(editor, 2);
        Check(editor.GetGuide().GetStepIndex() == 1, "picking again passes the step");
        Check(WaitUntilSettled(editor, 2), "the walk must reach the field again");
        Check(JBro::EditorActions::DeleteObject(editor, *object), "the object must be deleted");
        Tick(editor, 60);
        Check(editor.GetGuide().IsRunning() && editor.GetGuide().GetStepIndex() == 0, "deleting it goes back to picking too");

        // 마지막 단계(컴포넌트 추가)에서 선택을 비워도 돌아간다.
        JBro::GameObject* other = JBro::EditorActions::CreateObject(editor, nullptr);
        Check(other != nullptr, "another object must be made and picked");
        Tick(editor, 2);
        Check(editor.GetGuide().GetStepIndex() == 1, "the new pick passes the first step");
        editor.GetGuide().Update(editor, editor.GetGuideFocus(), JBro::GuideFocusAction::Next);
        Check(WaitUntilSettled(editor, 1), "the walk must reach the add component box");
        editor.ClearSelection();
        Tick(editor, 60);
        Check(editor.GetGuide().IsRunning() && editor.GetGuide().GetStepIndex() == 0,
            "losing the pick on the last step goes back to picking as well");
        editor.GetGuide().Stop(editor.GetGuideFocus());
    }

    bool NeverDone(JBro::EditorApplication&, JBro::GuideStepMemo&)
    {
        return false;
    }

    // 단추는 단계가 정한다. 둔 단추만 듣고, 막은 단추의 손짓은 무시한다. Esc 는 막을 수 없다.
    void TestEachStepChoosesItsButtons()
    {
        JBro::EditorApplication editor;
        HWND hwnd = nullptr;
        if (false == OpenEditor(editor, "GuideButtonsProbe", hwnd))
        {
            std::cout << "  [skip] no D3D12 device; guide buttons not verified" << std::endl;
            return;
        }
        JBro::Guide guide;
        guide.id = "test.buttons";
        {
            JBro::GuideStep first;
            first.path.Push(JBro::GuideFocusTargets::Panel("Hierarchy"));
            first.end = JBro::GuideStepEnd::NextButton;
            first.canSkip = false;
            guide.steps.Add(std::move(first));
        }
        {
            JBro::GuideStep second;
            second.path.Push(JBro::GuideFocusTargets::Panel("Inspector"));
            second.end = JBro::GuideStepEnd::Condition;
            second.condition = JBro::Delegate<bool(JBro::EditorApplication&, JBro::GuideStepMemo&)>::Bind<&NeverDone>();
            second.canGoBack = false;
            guide.steps.Add(std::move(second));
        }
        JBro::EditorGuide& run = editor.GetGuide();
        JBro::EditorGuideFocus& focus = editor.GetGuideFocus();
        Check(run.Start(guide, editor, focus), "the guide must start");
        Tick(editor, 2);
        Check(false == run.ShowsSkip(), "a step that forbids skipping shows no Skip");
        Check(run.ShowsBack() && false == run.CanGoBackNow(), "Back is shown on the first step but cannot be pressed");
        Check(run.ShowsNext(), "a step that ends with Next always shows Next, even without asking for it");
        run.Update(editor, focus, JBro::GuideFocusAction::Skip);
        Check(run.IsRunning(), "a Skip the step does not show is ignored");
        run.Update(editor, focus, JBro::GuideFocusAction::Back);
        Check(run.GetStepIndex() == 0, "Back on the first step goes nowhere");

        run.Update(editor, focus, JBro::GuideFocusAction::Next);
        Check(run.GetStepIndex() == 1, "Next goes on");
        Check(false == run.ShowsNext(), "a step that waits for its condition shows no Next unless it asks for one");
        Check(false == run.ShowsBack(), "a step that forbids going back shows no Back");
        Check(run.ShowsSkip(), "and Skip is there by default");
        run.Update(editor, focus, JBro::GuideFocusAction::Next);
        run.Update(editor, focus, JBro::GuideFocusAction::Back);
        Tick(editor, 3);
        Check(run.GetStepIndex() == 1, "neither a hidden Next nor a hidden Back moves the step");

        run.Update(editor, focus, JBro::GuideFocusAction::Skip);
        Check(false == run.IsRunning(), "Skip on the second step ends the guide");

        // 건너뛰기를 막은 첫 단계에서도 Esc 는 빠져나간다.
        Check(run.Start(guide, editor, focus), "the guide must start again");
        Check(false == run.ShowsSkip(), "on the step that forbids skipping");
        PostMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
        Tick(editor, 1);
        PostMessageW(hwnd, WM_KEYUP, VK_ESCAPE, static_cast<LPARAM>(0xC0000001u));
        Tick(editor, 1);
        Check(false == run.IsRunning() && false == focus.IsActive(), "Esc still leaves - there is always one way out");
    }

    void TestAStepTheUserOptsIntoCanBeNextedPastItsCondition()
    {
        JBro::EditorApplication editor;
        HWND hwnd = nullptr;
        if (false == OpenEditor(editor, "GuideManualNextProbe", hwnd))
        {
            std::cout << "  [skip] no D3D12 device; manual Next not verified" << std::endl;
            return;
        }
        JBro::Guide guide;
        guide.id = "test.manual_next";
        for (int index = 0; index < 2; ++index)
        {
            JBro::GuideStep step;
            step.path.Push(JBro::GuideFocusTargets::Panel(index == 0 ? "Hierarchy" : "Inspector"));
            step.end = JBro::GuideStepEnd::Condition;
            step.condition = JBro::Delegate<bool(JBro::EditorApplication&, JBro::GuideStepMemo&)>::Bind<&NeverDone>();
            step.canGoNext = true;
            guide.steps.Add(std::move(step));
        }
        JBro::EditorGuide& run = editor.GetGuide();
        JBro::EditorGuideFocus& focus = editor.GetGuideFocus();
        Check(run.Start(guide, editor, focus), "the guide must start");
        Check(run.ShowsNext(), "a condition step that opts into Next shows it");
        Tick(editor, 5);
        Check(run.GetStepIndex() == 0, "its condition never holds, so it waits");
        run.Update(editor, focus, JBro::GuideFocusAction::Next);
        Check(run.GetStepIndex() == 1, "and Next takes the user past it");
        run.Update(editor, focus, JBro::GuideFocusAction::Back);
        Check(run.GetStepIndex() == 0 && run.IsRevisiting(), "Back returns to it");
        run.Stop(focus);
    }

    // 이전으로 돌아온 단계는 조건이 이미 맞아도 저절로 넘어가지 않는다.
    void TestAStepReturnedToWaitsForNext()
    {
        JBro::EditorApplication editor;
        HWND hwnd = nullptr;
        if (false == OpenEditor(editor, "GuideBackProbe", hwnd))
        {
            std::cout << "  [skip] no D3D12 device; going back not verified" << std::endl;
            return;
        }
        JBro::GameObject* object = JBro::EditorActions::CreateObject(editor, nullptr);
        Check(object != nullptr, "the object must be made and picked");
        Tick(editor, 3);
        Check(editor.StartGuide("guide.add_component"), "the guide must start");
        Tick(editor, 2);
        editor.GetGuide().Update(editor, editor.GetGuideFocus(), JBro::GuideFocusAction::Next);
        Check(editor.GetGuide().GetStepIndex() == 1, "Next passes the first step for an object already picked");
        Check(editor.GetGuide().CanGoBackNow(), "the second step can go back");

        Check(WaitUntilSettled(editor, 2), "the hole must reach the field first");
        // 말풍선의 단추는 건너뛰기 · 이전 · 다음이다. 오른쪽에서 두 번째가 이전이다.
        Check(ClickBalloonButtonFromRight(editor, hwnd, 1), "the balloon's Back button must be found and pressed");
        Check(editor.GetGuide().GetStepIndex() == 0 && editor.GetGuide().IsRevisiting(), "Back returns to the first step");
        Tick(editor, 30);
        Check(editor.GetGuide().GetStepIndex() == 0,
            "the step returned to stays although its condition holds - bouncing forward would make Back look broken");
        Check(editor.GetGuide().ShowsNext(), "and offers Next instead");
        Check(WaitUntilSettled(editor, 0), "the hole goes back to the layers window");

        editor.GetGuide().Update(editor, editor.GetGuideFocus(), JBro::GuideFocusAction::Next);
        Check(editor.GetGuide().GetStepIndex() == 1 && false == editor.GetGuide().IsRevisiting(), "Next goes forward again");
        Check(object->GetComponents().Size() == 1, "going back and forth edits nothing");
        editor.GetGuide().Stop(editor.GetGuideFocus());
    }

    // 조건은 들어설 때와 비교한다. 이미 맞아 있던 상태로는 넘어가지 않는다.
    void TestConditionsAskWhatChangedSinceTheStepBegan()
    {
        JBro::EditorApplication editor;
        HWND hwnd = nullptr;
        if (false == OpenEditor(editor, "GuideMemoProbe", hwnd))
        {
            std::cout << "  [skip] no D3D12 device; step memos not verified" << std::endl;
            return;
        }
        JBro::GameObject* first = JBro::EditorActions::CreateObject(editor, nullptr);
        Check(first != nullptr && editor.GetSelectedObject() == first, "the first object must be made and picked");
        // 컴포넌트를 하나 더 붙여 둔다 - "둘 이상" 으로 물으면 마지막 단계가 아무것도 안 해도 끝난다.
        JBro::EditorActions::AddComponentList list;
        JBro::EditorActions::BuildAddComponentList(*first, list);
        std::size_t pick = list.typeNames.Size();
        std::size_t second = list.typeNames.Size();
        for (std::size_t index = 0; index < list.typeNames.Size(); ++index)
        {
            if (list.addable[index])
            {
                if (pick == list.typeNames.Size())
                {
                    pick = index;
                }
                else if (second == list.typeNames.Size())
                {
                    second = index;
                }
            }
        }
        Check(second < list.typeNames.Size(), "two components must be addable");
        Check(JBro::EditorActions::AddComponent(editor, *first, list.typeNames[pick]), "the extra component must attach");
        Check(first->GetComponents().Size() == 2, "the object holds two components before the guide");
        Tick(editor, 3);

        Check(editor.StartGuide("guide.add_component"), "the guide must start");
        Tick(editor, 10);
        Check(editor.GetGuide().GetStepIndex() == 0, "an object already picked does not pass the selection step");
        Check(editor.GetGuide().WhyNextBlocked(editor) == nullptr, "with one picked Next can be pressed");
        editor.ClearSelection();
        Tick(editor, 2);
        Check(editor.GetGuide().GetStepIndex() == 0, "picking nothing is not picking an object");
        Check(editor.GetGuide().WhyNextBlocked(editor) != nullptr, "and with nothing picked Next is blocked");
        Check(RightmostBalloonButtonIsDisabled(editor, hwnd), "and the balloon shows it grey");
        editor.GetGuide().Update(editor, editor.GetGuideFocus(), JBro::GuideFocusAction::Next);
        Check(editor.GetGuide().GetStepIndex() == 0, "so pressing it does not skip picking");
        // 새 오브젝트를 추가하면 그것이 선택된다 - 선택이 바뀌었으니 넘어간다.
        JBro::GameObject* added = JBro::EditorActions::CreateObject(editor, nullptr);
        Check(added != nullptr && editor.GetSelectedObject() == added, "the added object is picked");
        Tick(editor, 2);
        Check(editor.GetGuide().GetStepIndex() == 1, "a newly picked object passes the selection step");
        // 새 오브젝트(컴포넌트 하나)를 고른 채 마지막 단계로 간다.
        editor.GetGuide().Update(editor, editor.GetGuideFocus(), JBro::GuideFocusAction::Next);
        Check(editor.GetGuide().GetStepIndex() == 2, "on the add component step");
        Tick(editor, 10);
        Check(false == editor.GetGuide().IsConfirming(), "nothing was added yet");
        // 컴포넌트가 이미 둘인 첫 오브젝트로 옮겨 고른다. 붙인 것이 아니다.
        editor.SetSelectedObject(first);
        Tick(editor, 3);
        Check(false == editor.GetGuide().IsConfirming(),
            "picking another object that already holds more components does not count as adding one");
        // 옮겨 고른 오브젝트에 붙인 것은 붙인 것이다.
        std::size_t third = list.typeNames.Size();
        {
            JBro::EditorActions::AddComponentList now;
            JBro::EditorActions::BuildAddComponentList(*first, now);
            for (std::size_t index = 0; index < now.typeNames.Size(); ++index)
            {
                if (now.addable[index])
                {
                    third = index;
                    break;
                }
            }
            Check(third < now.typeNames.Size(), "a third component must be addable to the first object");
            Check(JBro::EditorActions::AddComponent(editor, *first, now.typeNames[third]), "the component must attach");
        }
        Tick(editor, 2);
        Check(editor.GetGuide().IsConfirming(), "adding to the object the user switched to counts");
        editor.GetGuide().Stop(editor.GetGuideFocus());
        Check(editor.StartGuide("guide.add_component"), "the guide must start again");
        editor.SetSelectedObject(added);
        Tick(editor, 2);
        editor.GetGuide().Update(editor, editor.GetGuideFocus(), JBro::GuideFocusAction::Next);
        editor.GetGuide().Update(editor, editor.GetGuideFocus(), JBro::GuideFocusAction::Next);
        Check(editor.GetGuide().GetStepIndex() == 2, "on the add component step again");
        Tick(editor, 3);
        Check(JBro::EditorActions::AddComponent(editor, *added, list.typeNames[second]), "the next component must attach");
        Tick(editor, 2);
        Check(editor.GetGuide().IsConfirming(), "adding one more is what the step waits for");
        editor.GetGuide().Stop(editor.GetGuideFocus());
    }

    // 키보드를 허용한 단계에서 글자를 치는 중이면 Esc 는 칸의 것이다(편집 취소). 치지 않을 때만 건너뛰기다.
    void TestEscapeWhileTypingBelongsToTheField()
    {
        EditorGuideFocus focus;
        GuideFocusPath path;
        Check(path.Push(A), "the path must take a target");
        Check(focus.Begin(path), "the focus must start");
        JBro::InputEvent escape;
        escape.kind = JBro::InputEventKind::KeyDown;
        escape.key = JBro::Key::Escape;
        JBro::Array<JBro::InputEvent> out;

        focus.SetKeyboardAllowed(true);
        focus.SetTextInputActive(true);
        focus.FilterInput({ &escape, 1 }, out);
        Check(out.Size() == 1 && false == focus.ConsumeSkipRequest(), "Esc while typing goes to the field and does not end the guide");

        focus.SetTextInputActive(false);
        focus.FilterInput({ &escape, 1 }, out);
        Check(out.Size() == 0 && focus.ConsumeSkipRequest(), "Esc outside a field still ends it");

        focus.SetKeyboardAllowed(false);
        focus.SetTextInputActive(true);
        focus.FilterInput({ &escape, 1 }, out);
        Check(out.Size() == 0 && focus.ConsumeSkipRequest(), "a step that holds the keyboard back keeps Esc for the guide");
    }

    struct Answer
    {
        bool close = false;
    };

    class AnswerPopup final : public JBro::EditorPopup
    {
    public:
        explicit AnswerPopup(Answer& answer)
            : m_answer(&answer)
        {
        }
        const char* GetTitle() const override
        {
            return "Answer";
        }
        const char* GetId() const override
        {
            return "guide.answer";
        }
        void OnDraw(JBro::EditorApplication&) override
        {
            ImGui::TextUnformatted("answer me");
            if (m_answer->close)
            {
                Close();
            }
        }

    private:
        Answer* m_answer = nullptr;
    };

    void TestAModalLiftsTheVeilUntilItCloses()
    {
        JBro::EditorApplication editor;
        HWND hwnd = nullptr;
        if (false == OpenEditor(editor, "GuideModalProbe", hwnd))
        {
            std::cout << "  [skip] no D3D12 device; the modal pause not verified" << std::endl;
            return;
        }
        Check(editor.StartGuide("guide.build_game"), "the guide must start");
        Check(WaitUntilSettled(editor, 0), "the hole must reach the File menu");
        Answer answer;
        Check(editor.OpenPopup(JBro::MakeOwnerPtr<AnswerPopup>(answer)) != JBro::InvalidPopupHandle, "the modal must open");
        Tick(editor, 3);
        Check(editor.GetGuideFocus().IsPaused(), "a modal pauses the guide focus");
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(500, 400));
        PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(500, 400));
        Tick(editor, 1);
        Check(ImGui::GetIO().MouseDown[0], "and the press reaches ImGui so the modal can be answered");
        PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(500, 400));
        Tick(editor, 20);
        Check(editor.GetGuideFocus().GetVeilAlpha() == 0.0f, "the veil is lifted while the modal waits");
        answer.close = true;
        Tick(editor, 4);
        Check(false == editor.GetGuideFocus().IsPaused(), "when the modal closes the guide focus resumes");
        Check(editor.GetGuide().IsRunning() && editor.GetGuideFocus().GetLevel() == 0, "where it was");
        Tick(editor, 20);
        Check(editor.GetGuideFocus().GetVeilAlpha() == 1.0f, "with the veil back");
        // 이 단계에는 다음 단추가 없다. 오른쪽 끝의 단추가 건너뛰기다.
        Check(ClickBalloonRightmostButton(editor, hwnd), "the balloon's Skip button must be found and pressed");
        Check(false == editor.GetGuide().IsRunning() && false == editor.GetGuideFocus().IsActive(), "Skip ends the guide");
    }
}

namespace
{
    // ── 글자로 적힌 가이드(D-267) ─────────────────────────────────────

    bool ParseGuide(const char* text, JBro::OwnerPtr<JBro::LoadedGuide>& out, JBro::String& error)
    {
        return JBro::EditorGuides::Parse(text, std::strlen(text), out, error);
    }

    bool Contains(const JBro::String& text, const char* part)
    {
        return text.find(part) != JBro::String::npos;
    }

    constexpr const char* TextBlock = R"(    Title:
      Key: test.title
      String: T
      Loc: en-US
    Body:
      Key: test.body
      String: B
      Loc: en-US
)";

    // 머리와 단계 하나를 붙여 한 편을 짓는다. `step` 은 대시 줄부터 쓴다(글자 블록은 뒤에 붙인다).
    JBro::String OneStepGuide(const char* step)
    {
        JBro::String text = "Id: test.guide\nTitle:\n  Key: test.guide_title\n  String: G\n  Loc: en-US\nSteps:\n";
        text += step;
        text += TextBlock;
        return text;
    }

    // 형식을 어기면 줄 번호와 까닭을 말하고 거절한다. 모르는 것을 짐작해 채우지 않는다.
    void TestAWrittenGuideIsReadOrRejectedWithAReason()
    {
        JBro::OwnerPtr<JBro::LoadedGuide> loaded;
        JBro::String error;
        const JBro::String good = OneStepGuide("  - Do: object.delete\n    Object: 12345\n    Via: canvas_view\n");
        Check(ParseGuide(good.c_str(), loaded, error), "a well-formed guide must be read");
        const JBro::Guide& guide = loaded->Get();
        Check(std::strcmp(guide.id, "test.guide") == 0 && guide.steps.Size() == 1, "its id and one step");
        const JBro::GuideStep& step = guide.steps[0];
        Check(std::strcmp(step.title.key, "test.title") == 0 && std::strcmp(step.title.string, "T") == 0
                && std::strcmp(step.title.locale, "en-US") == 0,
            "the title keeps its key, its text and the text's locale");
        Check(step.end == JBro::GuideStepEnd::Condition && step.condition.IsBound(), "deleting ends when the object is gone");
        Check(step.buildPath.IsBound() && step.nextRoute.IsBound(), "the path comes from the action, not from the text");
        Check(false == step.canGoNext && false == step.keyboard, "the action's defaults: no Next past a delete, no keyboard");

        struct Bad
        {
            const char* step;
            const char* reason;
        };
        const Bad bad[] = {
            { "  - Do: object.explode\n    Object: 1\n", "unknown action" },
            { "  - Do: object.delete\n", "needs Object" },
            { "  - Do: object.delete\n    Object: Player\n", "instance id or Selection" },
            { "  - Do: object.delete\n    Object: 1\n    Via: inspector\n", "cannot go by that route" },
            { "  - Do: game.build\n    Object: 1\n", "takes no Object" },
            { "  - Do: field.edit\n    End: done\n", "End must be" },
            { "  - Do: game.build\n    Colour: red\n", "unknown key 'Colour'" },
            { "  - Do: game.build\n    RetreatTo: later\n", "names no earlier step" },
            { "  - Do: game.build\n    Keyboard: maybe\n", "true or false" },
        };
        for (const Bad& entry : bad)
        {
            JBro::OwnerPtr<JBro::LoadedGuide> rejected;
            JBro::String reason;
            const JBro::String text = OneStepGuide(entry.step);
            Check(false == ParseGuide(text.c_str(), rejected, reason), entry.reason);
            Check(rejected.Get() == nullptr, "a rejected guide leaves nothing behind");
            if (false == Contains(reason, entry.reason) || false == Contains(reason, "line "))
            {
                std::cout << "  reason was: " << reason << std::endl;
            }
            Check(Contains(reason, entry.reason) && Contains(reason, "line "), entry.reason);
        }

        // 글자의 셋 중 하나라도 빠지면 거절한다. 키만 있으면 표에 없을 때 보일 것이 없다.
        const char* noLocale = "Id: g\nTitle:\n  Key: k\n  String: s\nSteps:\n  - Do: game.build\n";
        JBro::OwnerPtr<JBro::LoadedGuide> rejected;
        Check(false == ParseGuide(noLocale, rejected, error) && Contains(error, "'Loc'"), "a text without its locale is rejected");

        // 에이전트가 읽는 목록에 행동과 길이 다 있다.
        JBro::YamlWriter writer;
        JBro::EditorGuides::WriteCatalog(writer);
        const JBro::String& catalog = writer.GetText();
        Check(Contains(catalog, "Do: object.delete") && Contains(catalog, "- hierarchy") && Contains(catalog, "- canvas_view")
                && Contains(catalog, "- edit_menu"),
            "the catalog names delete and its three routes");
        Check(Contains(catalog, "Do: object.select") && Contains(catalog, "Do: component.add") && Contains(catalog, "Do: game.build")
                && Contains(catalog, "Do: field.edit"),
            "and every other action");
        // 목록은 다시 읽힌다 - 에이전트에게 넘길 때 같은 형식이다.
        JBro::YamlDocument document;
        JBro::YamlError yamlError;
        Check(document.Parse(catalog.c_str(), catalog.size(), yamlError), "the catalog is valid YAML of our subset");
    }

    // 원문의 로케일을 아는 글자 고르기. 표의 번역이 먼저이고, 원문이 지금 로케일이면 폴백 표보다 원문이다.
    void TestAWrittenTextPicksTheTableThenItsOwnWords()
    {
        const JBro::LocalizationTable& table = JBro::LocalizationTable::Get();
        const char* current = table.GetLocale().c_str();
        const char* known = table.FindInLocale(JBro::LocKeys::GuideSkip);
        if (known == nullptr)
        {
            std::cout << "  [skip] no localization table is loaded; written guide text not verified" << std::endl;
            return;
        }
        Check(std::strcmp(JBro::Loc::TextFor(JBro::LocKeys::GuideSkip, "own words", current), known) == 0,
            "a key the table knows shows the table's text");
        Check(std::strcmp(JBro::Loc::TextFor("test.nowhere", "own words", current), "own words") == 0,
            "a key it does not know shows the written words");
        Check(std::strcmp(JBro::Loc::TextFor("test.nowhere", "own words", "xx-XX"), "own words") == 0,
            "even when they are in another language - better than a bare key");
        Check(std::strcmp(JBro::Loc::TextFor(JBro::LocKeys::GuideSkip, "own words", nullptr), known) == 0,
            "no locale reads like TextOr");
    }

    JBro::String DeleteGuide(JBro::InstanceId id, const char* via)
    {
        char step[160] = {};
        std::snprintf(step, sizeof(step), "  - Do: object.delete\n    Object: %llu\n    Via: %s\n",
            static_cast<unsigned long long>(id), via);
        return OneStepGuide(step);
    }

    void RightClickAt(JBro::EditorApplication& editor, HWND hwnd, int x, int y)
    {
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
        Tick(editor, 1);
        PostMessageW(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(x, y));
        Tick(editor, 1);
        PostMessageW(hwnd, WM_RBUTTONUP, 0, MAKELPARAM(x, y));
        Tick(editor, 1);
    }

    bool HoleInsideWindow(const JBro::EditorGuideFocus& focus, const ImGuiWindow* window)
    {
        const Rect& hole = focus.GetHoleRect();
        const float pad = EditorGuideFocus::HolePadding + 1.0f;
        return window != nullptr && hole.min.x >= window->Pos.x - pad && hole.min.y >= window->Pos.y - pad
            && hole.max.x <= window->Pos.x + window->Size.x + pad && hole.max.y <= window->Pos.y + window->Size.y + pad;
    }

    // 우클릭 메뉴를 연 뒤 구멍이 그 안의 삭제로 옮겨 가고, 누르면 지워지고, 확인을 기다린다.
    void FinishDeleteThroughTheMenu(JBro::EditorApplication& editor, HWND hwnd, JBro::GameObject* object, std::uint32_t menuLevel)
    {
        const JBro::EditorGuideFocus& focus = editor.GetGuideFocus();
        Check(WaitUntilSettled(editor, menuLevel), "the hole must reach the object");
        Tick(editor, 60);
        Check(focus.GetLevel() == menuLevel, "the menu is never opened for the user - they right-click");
        int x = 0;
        int y = 0;
        HoleCenter(focus, x, y);
        // 왼쪽 단추로 누르는 것은 우클릭 메뉴를 열지 않는다.
        ClickAt(editor, hwnd, x, y);
        Tick(editor, 2);
        Check(focus.GetLevel() == menuLevel, "a left click on the object does not count as opening its menu");
        RightClickAt(editor, hwnd, x, y);
        Tick(editor, 2);
        Check(ImGui::GetCurrentContext()->OpenPopupStack.Size == 1, "right-clicking the object opens its menu");
        Check(WaitUntilSettled(editor, menuLevel + 1), "and the hole moves into the menu to Delete");
        const ImGuiWindow* menu = ImGui::GetCurrentContext()->OpenPopupStack[0].Window;
        Check(HoleInsideWindow(focus, menu), "the hole sits on an item of the open menu");
        // 메뉴의 첫 항목(오브젝트 추가)은 구멍 밖이다. 눌리지 않는다.
        const float firstItemY = menu->Pos.y + ImGui::GetStyle().WindowPadding.y + ImGui::GetFrameHeight() * 0.5f;
        const float menuX = menu->Pos.x + menu->Size.x * 0.5f;
        Check(firstItemY < focus.GetHoleRect().min.y, "the first item is above Delete, or the next check proves nothing");
        Check(false == focus.IsAllowed({ menuX, firstItemY }), "the other items of the object's menu are covered");

        const JBro::SafePtr<JBro::GameObject> watch = object->SafeFromThis();
        HoleCenter(focus, x, y);
        ClickAt(editor, hwnd, x, y);
        Tick(editor, 3);
        Check(watch.TryGet() == nullptr, "pressing Delete in the hole deletes the object");
        Check(editor.GetGuide().IsRunning() && editor.GetGuide().IsConfirming(), "the last step waits for OK instead of vanishing");
        // 지운 것의 항목은 사라졌다. 빈 테두리를 남기거나 끊긴 것으로 치지 않고 경로의 첫 칸으로 물러난다.
        Tick(editor, 45);
        Check(focus.GetPath().count == 1 && false == focus.IsBroken(),
            "with its target deleted, the done step steps back to the first level instead of breaking");
        Check(editor.GetGuide().IsConfirming(), "and still waits for OK");
        editor.GetGuide().Update(editor, editor.GetGuideFocus(), JBro::GuideFocusAction::Next);
        Check(false == editor.GetGuide().IsRunning(), "OK ends it");
        // 되돌리기로 살아난다 - 가이드가 편집을 대신하지 않고 커맨드를 거쳤다.
        Check(editor.GetCommands().CanUndo(), "the delete went through a command");
    }

    void TestADeleteGuideGoesByTheLayersWindow()
    {
        JBro::EditorApplication editor;
        HWND hwnd = nullptr;
        if (false == OpenEditor(editor, "GuideDeleteHierarchyProbe", hwnd))
        {
            std::cout << "  [skip] no D3D12 device; the delete guide by the layers window not verified" << std::endl;
            return;
        }
        JBro::GameObject* parent = JBro::EditorActions::CreateObject(editor, nullptr);
        JBro::GameObject* child = JBro::EditorActions::CreateObject(editor, parent);
        Check(child != nullptr && child->GetParent() == parent, "a parent and its child must be made");
        editor.SetSelectedObject(nullptr);
        Tick(editor, 3);

        JBro::String error;
        const JBro::String text = DeleteGuide(child->GetInstanceId(), "hierarchy");
        Check(editor.StartGuideFromText(text.c_str(), text.size(), error), error.c_str());
        const GuideFocusPath& path = editor.GetGuideFocus().GetPath();
        // 창 · 레이어 · 부모 · 그 줄의 메뉴 · 삭제
        Check(path.count == 5, "the layers window, the layer, the parent, the child's menu and Delete");
        Check(path.open[3] == GuideFocusOpen::User, "the child's menu is the user's to open");
        Check(path.targets[4] == JBro::GuideFocusTargets::Action("object.delete"), "and the path ends on Delete");
        FinishDeleteThroughTheMenu(editor, hwnd, child, 3);
        Check(parent->GetChildren().Size() == 0, "the parent is left and only the child went");
    }

    void TestADeleteGuideGoesByTheCanvasView()
    {
        JBro::EditorApplication editor;
        HWND hwnd = nullptr;
        if (false == OpenEditor(editor, "GuideDeleteCanvasProbe", hwnd))
        {
            std::cout << "  [skip] no D3D12 device; the delete guide by the canvas view not verified" << std::endl;
            return;
        }
        JBro::GameObject* object = JBro::EditorActions::CreateObject(editor, nullptr);
        Check(object != nullptr, "an object must be made");
        editor.SetSelectedObject(nullptr);
        Tick(editor, 3);

        JBro::String error;
        const JBro::String text = DeleteGuide(object->GetInstanceId(), "canvas_view");
        Check(editor.StartGuideFromText(text.c_str(), text.size(), error), error.c_str());
        const GuideFocusPath& path = editor.GetGuideFocus().GetPath();
        Check(path.count == 3 && path.targets[0] == JBro::GuideFocusTargets::Panel("CanvasView"),
            "the canvas view, the object in it and Delete");
        Check(WaitUntilSettled(editor, 1), "the hole must reach the object in the canvas view");
        Check(HoleInsideWindow(editor.GetGuideFocus(), ImGui::FindWindowByName("CanvasView")),
            "the hole is on the object drawn in the canvas view, clipped to the view");
        FinishDeleteThroughTheMenu(editor, hwnd, object, 1);
    }

    // 캔버스 뷰 밖에 있는 오브젝트를 가리키면 카메라가 그리로 간다. 구멍은 이웃 패널로 뚫리지 않는다.
    void TestAnObjectOffTheCanvasViewIsBroughtIntoView()
    {
        JBro::EditorApplication editor;
        HWND hwnd = nullptr;
        if (false == OpenEditor(editor, "GuideOffscreenProbe", hwnd))
        {
            std::cout << "  [skip] no D3D12 device; an object off the canvas view not verified" << std::endl;
            return;
        }
        JBro::ObjectPlacement distant;
        distant.hasPosition = true;
        distant.position[0] = 400.0f;
        distant.position[1] = -300.0f;
        JBro::GameObject* object = JBro::EditorActions::CreateObject(editor, nullptr, distant);
        Check(object != nullptr, "an object must be made far away");
        editor.SetSelectedObject(nullptr);
        Tick(editor, 3);

        JBro::String error;
        const JBro::String text = DeleteGuide(object->GetInstanceId(), "canvas_view");
        Check(editor.StartGuideFromText(text.c_str(), text.size(), error), error.c_str());
        // 처음에는 오브젝트가 뷰 밖이다 - 구멍이 뷰 안에 들어오면 카메라가 옮겨 간 것이다.
        Check(WaitUntilSettled(editor, 1), "the hole must reach the object once the camera has moved to it");
        Check(HoleInsideWindow(editor.GetGuideFocus(), ImGui::FindWindowByName("CanvasView")),
            "the object is brought into the view and the hole stays inside it");
        Check(editor.GetGuide().IsRunning() && editor.GetGuideFocus().GetPath().targets[0]
                == JBro::GuideFocusTargets::Panel("CanvasView"),
            "and the step did not break off to another route");
    }

    // 고른 길이 그려지지 않으면(계층 줄이 검색에 가려졌다) 같은 일에 드는 다음 길로 간다. 말없이 건너뛰지 않는다.
    void TestAHiddenRouteGivesWayToTheNext()
    {
        QuietLog quiet;
        JBro::EditorApplication editor;
        HWND hwnd = nullptr;
        if (false == OpenEditor(editor, "GuideRouteProbe", hwnd))
        {
            std::cout << "  [skip] no D3D12 device; route fallback not verified" << std::endl;
            return;
        }
        JBro::GameObject* object = JBro::EditorActions::CreateObject(editor, nullptr);
        Check(object != nullptr, "an object must be made");
        editor.SetSelectedObject(nullptr);
        Tick(editor, 3);

        // 계층의 검색 칸에 아무 이름과도 맞지 않는 글자를 친다. 줄이 모두 가려진다.
        const ImGuiWindow* hierarchy = ImGui::FindWindowByName("Hierarchy");
        Check(hierarchy != nullptr, "the layers window must be there");
        const int searchX = static_cast<int>(hierarchy->Pos.x + hierarchy->Size.x * 0.5f);
        const int searchY = static_cast<int>(hierarchy->Pos.y + hierarchy->TitleBarHeight + ImGui::GetStyle().WindowPadding.y
            + ImGui::GetFrameHeight() * 0.5f);
        ClickAt(editor, hwnd, searchX, searchY);
        for (const wchar_t letter : { L'q', L'z', L'x' })
        {
            PostMessageW(hwnd, WM_CHAR, letter, 0);
            Tick(editor, 1);
        }
        Tick(editor, 3);
        Check(ImGui::GetIO().WantTextInput, "the search box must have taken the letters");
        PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
        Tick(editor, 1);
        PostMessageW(hwnd, WM_KEYUP, VK_RETURN, static_cast<LPARAM>(0xC0000001u));
        Tick(editor, 2);

        JBro::String error;
        const JBro::String text = DeleteGuide(object->GetInstanceId(), "auto");
        Check(editor.StartGuideFromText(text.c_str(), text.size(), error), error.c_str());
        Check(editor.GetGuideFocus().GetPath().targets[0] == JBro::GuideFocusTargets::Panel("Hierarchy"),
            "auto starts with the first route, the layers window");
        Tick(editor, 60);
        const GuideFocusPath& path = editor.GetGuideFocus().GetPath();
        Check(editor.GetGuide().IsRunning(), "the step is not skipped");
        Check(path.targets[0] == JBro::GuideFocusTargets::Panel("CanvasView"), "it goes by the canvas view instead");
        FinishDeleteThroughTheMenu(editor, hwnd, object, 1);
    }

    // 편집 메뉴로 가는 길: 선택한 것을 지우는 항목이라 가리킨 오브젝트 하나만 고른 뒤, 사용자가 메뉴를 열면 삭제로 간다.
    // `Object: Selection` 은 단계에 들어설 때 선택한 오브젝트다.
    void TestADeleteGuideGoesByTheEditMenu()
    {
        JBro::EditorApplication editor;
        HWND hwnd = nullptr;
        if (false == OpenEditor(editor, "GuideDeleteMenuProbe", hwnd))
        {
            std::cout << "  [skip] no D3D12 device; the delete guide by the edit menu not verified" << std::endl;
            return;
        }
        JBro::GameObject* keep = JBro::EditorActions::CreateObject(editor, nullptr);
        JBro::GameObject* object = JBro::EditorActions::CreateObject(editor, nullptr);
        Check(keep != nullptr && object != nullptr, "two objects must be made");
        editor.SetSelectedObject(object);
        editor.AddToSelection(keep);
        Check(editor.GetSelectedObjects().Size() == 2, "both are picked, or the next check proves nothing");
        Tick(editor, 3);

        const JBro::String text = OneStepGuide("  - Do: object.delete\n    Object: Selection\n    Via: edit_menu\n");
        JBro::String error;
        Check(editor.StartGuideFromText(text.c_str(), text.size(), error), error.c_str());
        const GuideFocusPath& path = editor.GetGuideFocus().GetPath();
        Check(path.count == 2 && path.targets[0] == JBro::GuideFocusTargets::Menu("menu.edit")
                && path.open[0] == GuideFocusOpen::User,
            "the Edit menu, which the user opens, and Delete in it");
        Check(editor.GetSelectedObjects().Size() == 1 && editor.GetSelectedObject() == object,
            "only the object to delete stays picked - the menu item deletes every picked object");

        const JBro::EditorGuideFocus& focus = editor.GetGuideFocus();
        Check(WaitUntilSettled(editor, 0), "the hole must reach the Edit menu");
        int x = 0;
        int y = 0;
        HoleCenter(focus, x, y);
        ClickAt(editor, hwnd, x, y);
        Tick(editor, 2);
        Check(WaitUntilSettled(editor, 1), "opening the menu moves the hole onto Delete");
        const JBro::SafePtr<JBro::GameObject> watch = object->SafeFromThis();
        HoleCenter(focus, x, y);
        ClickAt(editor, hwnd, x, y);
        Tick(editor, 3);
        Check(watch.TryGet() == nullptr, "pressing Delete deletes the object");
        Check(keep->SafeFromThis().TryGet() != nullptr, "and only that one");
        Check(editor.GetGuide().IsConfirming(), "the step is done and waits for OK");
    }

    // 정해 둔 오브젝트를 고르는 단계: 다른 것을 골라서는 넘어가지 않고, 다음도 막힌다. 그것을 고르면 넘어간다.
    void TestSelectingAGivenObjectWaitsForThatObject()
    {
        JBro::EditorApplication editor;
        HWND hwnd = nullptr;
        if (false == OpenEditor(editor, "GuideSelectProbe", hwnd))
        {
            std::cout << "  [skip] no D3D12 device; selecting a given object not verified" << std::endl;
            return;
        }
        JBro::GameObject* wanted = JBro::EditorActions::CreateObject(editor, nullptr);
        JBro::GameObject* other = JBro::EditorActions::CreateObject(editor, nullptr);
        Check(wanted != nullptr && other != nullptr, "two objects must be made");
        editor.SetSelectedObject(nullptr);
        Tick(editor, 3);

        char step[96] = {};
        std::snprintf(step, sizeof(step), "  - Do: object.select\n    Object: %llu\n",
            static_cast<unsigned long long>(wanted->GetInstanceId()));
        const JBro::String text = OneStepGuide(step);
        JBro::String error;
        Check(editor.StartGuideFromText(text.c_str(), text.size(), error), error.c_str());
        const GuideFocusPath& path = editor.GetGuideFocus().GetPath();
        Check(path.targets[path.count - 1]
                == JBro::GuideFocusTargets::HierarchyObject(editor.GetObjectIds().Track(wanted)),
            "the path ends on the wanted object's row");
        JBro::EditorGuide& run = editor.GetGuide();
        Tick(editor, 2);
        editor.SetSelectedObject(other);
        Tick(editor, 2);
        Check(run.IsRunning() && false == run.IsConfirming(), "picking another object does not finish the step");
        Check(run.WhyNextBlocked(editor) != nullptr, "and Next is held back until the wanted one is picked");
        editor.SetSelectedObject(wanted);
        Tick(editor, 2);
        Check(run.IsConfirming(), "picking the wanted object finishes it");

        // 읽지 못한 가이드는 돌던 가이드를 건드리지 않는다.
        const char* broken = "Id: x\nSteps:\n  - Do: nothing\n";
        Check(false == editor.StartGuideFromText(broken, std::strlen(broken), error), "a broken text is refused");
        Check(run.IsRunning() && run.IsConfirming(), "and the running guide goes on");
        editor.CloseProject();
        Check(false == run.IsRunning(), "closing the project stops it");
    }
}

int RunEditorGuideTests()
{
    TestAClosedLevelOpensOnlyAfterTheHoleSettlesAndDwells();
    TestALevelTheUserOpensWaitsForTheUser();
    TestClosingAWalkedLevelGoesBackToIt();
    TestALevelThatIsNeverDrawnBreaksThePath();
    TestTheAllowedAreaIsWhereTheHoleIsGoingNotWhereItIs();
    TestOnlyPopupsTheTargetOpensAreOpenToTheUser();
    TestReportsOutsideThePathOrTwiceAreIgnored();
    TestTheVeilFadesInAndOut();
    TestPausingLiftsTheVeilAndOpensTheGate();
    TestTheAddComponentGuideWalksTheInspector();
    TestTheBuildGuideWaitsForTheUserToOpenTheMenu();
    TestAPathToAChildObjectWalksTheHierarchy();
    TestABrokenStepIsSkipped();
    TestLosingTheObjectGoesBackToPicking();
    TestEscapeWhileTypingBelongsToTheField();
    TestEachStepChoosesItsButtons();
    TestAStepTheUserOptsIntoCanBeNextedPastItsCondition();
    TestAStepReturnedToWaitsForNext();
    TestConditionsAskWhatChangedSinceTheStepBegan();
    TestAModalLiftsTheVeilUntilItCloses();
    TestAWrittenGuideIsReadOrRejectedWithAReason();
    TestAWrittenTextPicksTheTableThenItsOwnWords();
    TestADeleteGuideGoesByTheLayersWindow();
    TestADeleteGuideGoesByTheCanvasView();
    TestAnObjectOffTheCanvasViewIsBroughtIntoView();
    TestAHiddenRouteGivesWayToTheNext();
    TestADeleteGuideGoesByTheEditMenu();
    TestSelectingAGivenObjectWaitsForThatObject();
    std::cout << "Editor guide tests passed.\n";
    return 0;
}
