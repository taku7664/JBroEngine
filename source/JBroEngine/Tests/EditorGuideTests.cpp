#include <JBro/Core/Log.h>
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

    // 말풍선의 맨 아래 줄을 오른쪽 끝부터 훑어, 누를 수 있는 단추 가운데 오른쪽에서 `skip` 개를 지난 것을 누른다
    // (0 이면 맨 오른쪽 - 다음이 있으면 다음이다). 회색 단추는 세지 않는다.
    bool ClickBalloonButtonFromRight(JBro::EditorApplication& editor, HWND hwnd, int skip)
    {
        const ImGuiWindow* balloon = ImGui::FindWindowByName("##guide_focus_balloon");
        if (balloon == nullptr || false == balloon->WasActive)
        {
            return false;
        }
        const float y = balloon->Pos.y + balloon->Size.y - 12.0f - ImGui::GetFrameHeight() * 0.5f;
        ImGuiID last = 0;
        int seen = -1;
        for (float x = balloon->Pos.x + balloon->Size.x - 16.0f; x > balloon->Pos.x; x -= 3.0f)
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
        Tick(editor, 2);
        // 오브젝트를 이미 골라 두었으니 첫 단계는 곧 지나간다.
        Check(editor.GetGuide().GetStepIndex() == 1, "with an object picked the first step passes at once");
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

        Check(ClickBalloonRightmostButton(editor, hwnd), "the balloon's Next button must be found and pressed");
        Check(editor.GetGuide().GetStepIndex() == 2, "Next goes on to adding a component");
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
        JBro::GameObject* object = JBro::EditorActions::CreateObject(editor, nullptr);
        Check(object != nullptr, "the object must be made");
        Tick(editor, 3);
        Check(editor.StartGuide("guide.add_component"), "the guide must start");
        Check(WaitUntilSettled(editor, 2), "the walk must reach the field");
        Check(editor.GetGuide().GetStepIndex() == 1, "on the second step");
        // 가리키던 오브젝트를 지운다. 필드도 컴포넌트 추가 칸도 더는 그려지지 않는다.
        Check(JBro::EditorActions::DeleteObject(editor, *object), "the object must be deleted");
        Tick(editor, 150);
        Check(false == editor.GetGuide().IsRunning(), "steps whose target is gone are skipped to the end, not waited on forever");
        Check(false == editor.GetGuideFocus().IsActive(), "and the veil goes with them");
        const char* last = editor.GetNotifications().GetLastTitle();
        Check(last == nullptr || std::strcmp(last, JBro::Loc::TextOr(JBro::LocKeys::GuideFinished, "Guide finished")) != 0,
            "a guide that ran out of targets is not announced as finished");
    }

    bool NeverDone(JBro::EditorApplication&)
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
            second.condition = JBro::Delegate<bool(JBro::EditorApplication&)>::Bind<&NeverDone>();
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
            step.condition = JBro::Delegate<bool(JBro::EditorApplication&)>::Bind<&NeverDone>();
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
        Check(editor.GetGuide().GetStepIndex() == 1, "with an object picked the first step passes at once");
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
    TestEachStepChoosesItsButtons();
    TestAStepTheUserOptsIntoCanBeNextedPastItsCondition();
    TestAStepReturnedToWaitsForNext();
    TestAModalLiftsTheVeilUntilItCloses();
    std::cout << "Editor guide tests passed.\n";
    return 0;
}
