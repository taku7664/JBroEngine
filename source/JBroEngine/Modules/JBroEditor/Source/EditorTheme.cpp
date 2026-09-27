#include <JBro/Editor/EditorTheme.h>
#include <cstring>
#include <JBro/Platform/Platform.h>
#include <JBro/Editor/EditorIcons.h>

#include <imgui.h>

#include <cstddef>
#include <cstdio>

namespace JBro::EditorTheme
{
    void ApplyColors()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        colors[ImGuiCol_Text]                   = Ink;
        colors[ImGuiCol_TextDisabled]           = InkMuted;

        colors[ImGuiCol_WindowBg]               = Panel;
        colors[ImGuiCol_ChildBg]                = Clear;
        colors[ImGuiCol_PopupBg]                = Raised;
        colors[ImGuiCol_Border]                 = Line;
        colors[ImGuiCol_BorderShadow]           = Clear;

        colors[ImGuiCol_FrameBg]                = Input;
        colors[ImGuiCol_FrameBgHovered]         = Hover;
        colors[ImGuiCol_FrameBgActive]          = Pressed;

        // **도크의 탭 띠 바탕이 이 색이다**(떠 있는 창의 제목 줄도 같이 쓴다).
        // 초점이 있으면 `TitleBgActive` 로 한 단만 올린다 - 고른 탭까지 올리면 둘이
        // 같은 면이 되어 어느 탭이 열려 있는지 보이지 않는다.
        colors[ImGuiCol_TitleBg]                = TabStrip;
        colors[ImGuiCol_TitleBgActive]          = TabStripActive;
        colors[ImGuiCol_TitleBgCollapsed]       = TabStrip;
        // 메뉴 줄은 탭 띠보다 위의 면이다. 같은 색이면 두 띠가 한 덩어리로 보인다.
        colors[ImGuiCol_MenuBarBg]              = Raised;

        colors[ImGuiCol_ScrollbarBg]            = Workspace;
        colors[ImGuiCol_ScrollbarGrab]          = Hover;
        colors[ImGuiCol_ScrollbarGrabHovered]   = LineStrong;
        colors[ImGuiCol_ScrollbarGrabActive]    = Fade(Accent, 0.80f);

        // 체크된 칸은 파란 바탕에 흰 표시다. 예전에는 표시만 금색이었는데,
        // 그러면 늘 켜져 있는 자리가 경고와 같은 색을 갖는다.
        colors[ImGuiCol_CheckMark]              = Ink;
        colors[ImGuiCol_CheckboxSelectedBg]     = Accent;
        colors[ImGuiCol_SliderGrab]             = Accent;
        colors[ImGuiCol_SliderGrabActive]       = AccentBright;

        colors[ImGuiCol_Button]                 = Input;
        colors[ImGuiCol_ButtonHovered]          = Hover;
        colors[ImGuiCol_ButtonActive]           = Pressed;

        // 고른 줄이다(계층·목록·메뉴 항목). 바탕을 갈지 않고 파랑을 덮는다 -
        // 줄마다 바탕색이 바뀌면 밝기 계층이 흐트러진다.
        colors[ImGuiCol_Header]                 = Fade(Accent, 0.38f);
        colors[ImGuiCol_HeaderHovered]          = Fade(Accent, 0.24f);
        colors[ImGuiCol_HeaderActive]           = Fade(Accent, 0.52f);

        colors[ImGuiCol_Separator]              = Line;
        colors[ImGuiCol_SeparatorHovered]       = Fade(Accent, 0.70f);
        colors[ImGuiCol_SeparatorActive]        = Accent;

        colors[ImGuiCol_ResizeGrip]             = Fade(Accent, 0.20f);
        colors[ImGuiCol_ResizeGripHovered]      = Fade(Accent, 0.55f);
        colors[ImGuiCol_ResizeGripActive]       = Fade(Accent, 0.85f);

        colors[ImGuiCol_InputTextCursor]        = Ink;

        // 탭은 띠 위에 올라앉은 면이다. 고르지 않은 탭도 제 면을 가진다 - 띠와 같은
        // 색이면 탭이 아니라 띠에 얹힌 글자로 보이고, 탭 사이의 틈만 도드라진다.
        colors[ImGuiCol_Tab]                    = TabIdle;
        colors[ImGuiCol_TabHovered]             = Hover;
        colors[ImGuiCol_TabSelected]            = TabTop;
        colors[ImGuiCol_TabSelectedOverline]    = Accent;
        colors[ImGuiCol_TabDimmed]              = TabStripActive;
        colors[ImGuiCol_TabDimmedSelected]      = Raised;
        // 초점이 없는 탭바의 선은 회색이다. 파랑은 초점이 있다는 뜻이다.
        colors[ImGuiCol_TabDimmedSelectedOverline] = LineStrong;

        colors[ImGuiCol_DockingPreview]         = Fade(Accent, 0.40f);
        colors[ImGuiCol_DockingEmptyBg]         = Workspace;

        colors[ImGuiCol_PlotLines]              = InkMuted;
        colors[ImGuiCol_PlotLinesHovered]       = AccentBright;
        colors[ImGuiCol_PlotHistogram]          = Accent;
        colors[ImGuiCol_PlotHistogramHovered]   = AccentBright;

        colors[ImGuiCol_TableHeaderBg]          = Raised;
        colors[ImGuiCol_TableBorderStrong]      = LineStrong;
        colors[ImGuiCol_TableBorderLight]       = Line;
        colors[ImGuiCol_TableRowBg]             = Clear;
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

        colors[ImGuiCol_TextLink]               = AccentBright;
        colors[ImGuiCol_TextSelectedBg]         = Fade(Accent, 0.35f);
        colors[ImGuiCol_TreeLines]              = Line;

        colors[ImGuiCol_DragDropTarget]         = AccentBright;
        colors[ImGuiCol_DragDropTargetBg]       = Fade(Accent, 0.15f);

        // 저장하지 않은 표시다. 주의를 끌어야 하므로 호박색이다.
        colors[ImGuiCol_UnsavedMarker]          = Amber;

        colors[ImGuiCol_NavCursor]              = Fade(Accent, 0.80f);
        colors[ImGuiCol_NavWindowingHighlight]  = Fade(Ink, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg]      = Fade(Workspace, 0.60f);
        colors[ImGuiCol_ModalWindowDimBg]       = Fade(Workspace, 0.60f);
    }

    void ApplyLayout()
    {
        ImGuiStyle& style = ImGui::GetStyle();

        // 창
        style.WindowPadding            = ImVec2(8.0f, 8.0f);
        // **모서리를 둥글리지 않는다.** 패널은 서로 붙어 도킹되므로, 둥근 모서리는
        // 맞닿은 자리마다 바탕이 비치는 틈을 만든다.
        style.WindowRounding           = 0.0f;
        style.WindowBorderSize         = 1.0f;
        style.WindowBorderHoverPadding = 4.0f;
        // 패널을 작게 끌어도 접히지 않게.
        style.WindowMinSize            = ImVec2(80.0f, 40.0f);
        style.WindowTitleAlign         = ImVec2(0.0f, 0.5f);
        // 제목 왼쪽의 접기 화살표를 없앤다. 패널에는 쓸 일이 없다.
        style.WindowMenuButtonPosition = ImGuiDir_None;

        style.ChildRounding            = 0.0f;
        style.ChildBorderSize          = 1.0f;

        // 팝업은 떠 있는 면이라 모서리가 둥글다. 도킹되지 않으므로 틈이 생기지 않는다.
        style.PopupRounding            = 4.0f;
        style.PopupBorderSize          = 1.0f;

        style.DisplayWindowPadding     = ImVec2(12.0f, 12.0f);
        style.DisplaySafeAreaPadding   = ImVec2(3.0f, 3.0f);

        // 위젯과 간격
        style.FramePadding             = ImVec2(6.0f, 4.0f);
        style.FrameRounding            = 3.0f;
        style.FrameBorderSize          = 0.0f;

        style.ItemSpacing              = ImVec2(8.0f, 6.0f);
        style.ItemInnerSpacing         = ImVec2(6.0f, 4.0f);
        style.CellPadding              = ImVec2(6.0f, 4.0f);

        style.TouchExtraPadding        = ImVec2(0.0f, 0.0f);
        style.IndentSpacing            = 18.0f;
        style.ColumnsMinSpacing        = 6.0f;

        // 스크롤바와 손잡이
        style.ScrollbarSize            = 12.0f;
        style.ScrollbarRounding        = 6.0f;
        style.ScrollbarPadding         = 2.0f;

        style.GrabMinSize              = 18.0f;
        style.GrabRounding             = 3.0f;
        style.LogSliderDeadzone        = 4.0f;

        // **탭의 위 모서리는 둥글다.** 브라우저의 탭이 그렇듯, 둥근 위와 반듯한 아래가
        // "띠에 꽂힌 한 장" 으로 읽힌다. 각진 탭은 띠를 칸으로 나눈 것처럼 보였다.
        style.TabRounding              = 6.0f;
        style.TabBorderSize            = 0.0f;
        style.TabMinWidthBase          = 1.0f;
        style.TabMinWidthShrink        = 80.0f;
        style.TabCloseButtonMinWidthSelected   = 0.0f;
        style.TabCloseButtonMinWidthUnselected = 0.0f;
        // 띠와 패널을 가르는 선이다. 초점이 있으면 고른 탭의 색을 띤다.
        style.TabBarBorderSize         = 2.0f;
        style.TabBarOverlineSize       = 2.0f;

        // 트리와 구분선
        style.TreeLinesFlags           = ImGuiTreeNodeFlags_DrawLinesToNodes;
        style.TreeLinesSize            = 1.0f;
        style.TreeLinesRounding        = 2.0f;

        style.SeparatorSize            = 1.0f;
        style.SeparatorTextBorderSize  = 1.0f;
        style.SeparatorTextAlign       = ImVec2(0.0f, 0.5f);
        style.SeparatorTextPadding     = ImVec2(8.0f, 5.0f);

        // 나머지 위젯
        style.MenuItemRounding         = 2.0f;
        style.SelectableRounding       = 2.0f;

        style.ImageRounding            = 3.0f;
        style.ImageBorderSize          = 0.0f;

        style.DragDropTargetRounding   = 3.0f;
        style.DragDropTargetBorderSize = 2.0f;
        style.DragDropTargetPadding    = 3.0f;

        style.ButtonTextAlign          = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign      = ImVec2(0.0f, 0.5f);

        style.InputTextCursorSize      = 1.5f;

        // 도킹
        style.DockingSeparatorSize     = 1.0f;

        // 전역
        style.Alpha                    = 1.0f;
        style.DisabledAlpha            = 0.55f;

        // 툴팁이 뜨는 때
        style.HoverStationaryDelay     = 0.20f;
        style.HoverDelayShort          = 0.25f;
        style.HoverDelayNormal         = 0.50f;
    }

    namespace
    {
        const char* g_iconFontPath = nullptr;
        bool g_hasIconFont = false;
        // 글꼴 파일을 읽어 줄 플랫폼이다. 널이면 아이콘 없이 간다.
        IPlatform* g_platform = nullptr;
    }

    void SetIconFontPath(const char* path, IPlatform* platform)
    {
        g_iconFontPath = path;
        g_platform = platform;
    }

    bool HasIconFont()
    {
        return g_hasIconFont;
    }

    // 아이콘 글꼴을 본문 글꼴에 합친다(D-96). `MergeMode` 라 같은 `ImFont` 안에서 U+F000..F8FF
    // 만 이 파일에서 온다. 파일이 없으면 합치지 않고 아이콘 자리에 네모가 나온다 - 그래도
    // 에디터는 뜬다. 기존 엔진 `ImEditor` 의 같은 자리를 옮겼다.
    void MergeIconFont()
    {
        g_hasIconFont = false;
        if (g_iconFontPath == nullptr || *g_iconFontPath == '\0')
        {
            return;
        }
        // **파일은 플랫폼이 읽는다**(D-176, D-112). 예전에는 `fopen_s` 로 열었는데, 그것은 경로를
        // 이 기계의 ANSI 코드페이지로 본다 - 사용자 폴더에 한글이 있으면(`C:/Users/박주형/...`)
        // UTF-8 경로가 맞지 않아 **글꼴을 못 찾았다**. 실제로 실행 파일 기준 절대경로로 바꾼
        // 날 그 자리에서 났다. 플랫폼의 읽기는 UTF-8 을 넓은 문자로 제대로 바꾼다.
        Array<std::byte> bytes;
        if (g_platform == nullptr || false == g_platform->ReadWholeFile(g_iconFontPath, bytes)
            || bytes.IsEmpty())
        {
            std::printf("note: icon font not found at %s; icons render as boxes\n", g_iconFontPath);
            return;
        }
        const std::size_t fileSize = bytes.Size();
        // 아틀라스가 이 메모리를 물려받아 `IM_FREE` 로 놓는다. 그래서 ImGui 의 할당기로 잡는다.
        void* fontData = ImGui::MemAlloc(fileSize);
        if (fontData == nullptr)
        {
            std::printf("note: icon font at %s could not be read; icons render as boxes\n", g_iconFontPath);
            return;
        }
        std::memcpy(fontData, bytes.Data(), fileSize);

        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig config;
        config.MergeMode = true;
        config.PixelSnapH = true;
        // 아이콘은 글자보다 조금 작게 그려야 줄 높이를 밀지 않는다.
        config.GlyphMinAdvanceX = 13.0f;
        static const ImWchar ranges[] = {Icons::RangeBegin, Icons::RangeEnd, 0};
        if (io.Fonts->AddFontFromMemoryTTF(fontData, static_cast<int>(fileSize), 13.0f, &config, ranges)
            != nullptr)
        {
            g_hasIconFont = true;
            return;
        }
        std::printf("note: icon font at %s could not be read; icons render as boxes\n", g_iconFontPath);
    }

    bool ApplyFont()
    {
        ImGuiIO& io = ImGui::GetIO();

        ImFontConfig config;
        // 기존 엔진이 눈으로 맞춘 값이다. 작은 글자가 뭉개지지 않는다.
        config.OversampleH = 3;
        config.OversampleV = 3;
        config.PixelSnapH = true;

        // **글리프 범위를 주지 않는다.** 기존 엔진은 한글 자모·음절 범위를 손으로
        // 적어 넘겼는데, 그때의 ImGui 는 아틀라스를 미리 구워야 했기 때문이다.
        // 우리 백엔드는 `RendererHasTextures` 를 켜서 글자가 필요할 때 올라간다 -
        // 범위를 적으면 쓰지도 않을 글리프를 미리 굽고, 적지 않은 글자는 못 쓴다.
        if (io.Fonts->AddFontFromFileTTF(
                "C:\\Windows\\Fonts\\malgun.ttf", 15.0f, &config) != nullptr)
        {
            MergeIconFont();
            return true;
        }

        // 글꼴이 없는 기계다. 기본 글꼴로 남긴다 - 한글은 네모로 나오지만
        // 에디터는 뜬다. 여기서 멈추면 글꼴 하나 때문에 아무것도 못 본다.
        std::printf("note: malgun.ttf not found; the editor falls back to the "
            "built-in font and Korean text will not render\n");
        io.Fonts->AddFontDefault();
        MergeIconFont();
        return false;
    }

    void Apply()
    {
        ApplyColors();
        ApplyLayout();
        ApplyFont();
    }
}
