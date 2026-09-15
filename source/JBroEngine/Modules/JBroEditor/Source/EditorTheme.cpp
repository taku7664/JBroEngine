#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/EditorIcons.h>

#include <imgui.h>

#include <cstdio>

namespace JBro::EditorTheme
{
    void ApplyColors()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.11f, 0.12f, 0.15f, 1.00f);
        colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_PopupBg]                = ImVec4(0.11f, 0.12f, 0.15f, 1.00f);
        colors[ImGuiCol_Border]                 = ImVec4(0.27f, 0.31f, 0.39f, 0.47f);
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.22f, 0.23f, 0.27f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.29f, 0.30f, 0.36f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.18f, 0.19f, 0.24f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.11f, 0.12f, 0.15f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.13f, 0.15f, 0.19f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.22f, 0.23f, 0.27f, 1.00f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.16f, 0.20f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.80f, 0.64f, 0.27f, 1.00f);
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.80f, 0.63f, 0.27f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.87f, 0.72f, 0.40f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.22f, 0.23f, 0.27f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.31f, 0.33f, 0.37f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.18f, 0.19f, 0.24f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.17f, 0.22f, 0.27f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.25f, 0.31f, 0.36f, 1.00f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.15f, 0.19f, 0.23f, 1.00f);
        colors[ImGuiCol_Separator]              = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.14f, 0.21f, 0.29f, 1.00f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.31f, 0.41f, 0.52f, 1.00f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.11f, 0.17f, 0.24f, 1.00f);
        colors[ImGuiCol_InputTextCursor]        = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.32f, 0.37f, 0.43f, 1.00f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.23f, 0.27f, 0.31f, 0.00f);
        colors[ImGuiCol_TabSelected]            = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
        colors[ImGuiCol_TabSelectedOverline]    = ImVec4(0.52f, 0.57f, 0.62f, 1.00f);
        colors[ImGuiCol_TabDimmed]              = ImVec4(0.16f, 0.19f, 0.21f, 0.00f);
        colors[ImGuiCol_TabDimmedSelected]      = ImVec4(0.23f, 0.27f, 0.31f, 1.00f);
        colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
        colors[ImGuiCol_DockingPreview]         = ImVec4(0.04f, 0.52f, 1.00f, 0.51f);
        colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
        colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
        colors[ImGuiCol_TextLink]               = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
        colors[ImGuiCol_TreeLines]              = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
        colors[ImGuiCol_DragDropTarget]         = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
        colors[ImGuiCol_NavCursor]              = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
        colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    }

    void ApplyLayout()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 3.0f;
        style.ChildRounding = 3.0f;
        style.FrameRounding = 3.0f;
        style.PopupRounding = 3.0f;
        style.GrabRounding = 3.0f;

        // 탭만 각지다. 위에 선을 얹어 고른 탭을 표시한다.
        style.TabRounding = 0.0f;
        style.TabBarOverlineSize = 2.5f;
        style.TabCloseButtonMinWidthSelected = 0.0f;
        style.TabCloseButtonMinWidthUnselected = 0.0f;

        style.TreeLinesSize = 1.0f;
        style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesToNodes;

        // 제목 왼쪽의 접기 화살표를 없앤다. 패널에는 쓸 일이 없다.
        style.WindowMenuButtonPosition = ImGuiDir_None;

        style.DockingSeparatorSize = 1.0f;
        style.SeparatorSize = 1.0f;
        style.SeparatorTextBorderSize = 1.0f;

        // 패널을 아주 작게 끌어도 접히지 않게.
        style.WindowMinSize = ImVec2(60.0f, 30.0f);
    }

    namespace
    {
        const char* g_iconFontPath = nullptr;
        bool g_hasIconFont = false;
    }

    void SetIconFontPath(const char* path)
    {
        g_iconFontPath = path;
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
        // **파일이 있는지 먼저 본다.** ImGui 는 없는 파일을 열려 하면 단언으로 죽는다 -
        // 아이콘 글꼴 하나 때문에 에디터가 뜨지 않으면 안 된다.
        FILE* probe = nullptr;
        if (fopen_s(&probe, g_iconFontPath, "rb") != 0 || probe == nullptr)
        {
            std::printf("note: icon font not found at %s; icons render as boxes\n", g_iconFontPath);
            return;
        }
        std::fclose(probe);
        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig config;
        config.MergeMode = true;
        config.PixelSnapH = true;
        // 아이콘은 글자보다 조금 작게 그려야 줄 높이를 밀지 않는다.
        config.GlyphMinAdvanceX = 13.0f;
        static const ImWchar ranges[] = {Icons::RangeBegin, Icons::RangeEnd, 0};
        if (io.Fonts->AddFontFromFileTTF(g_iconFontPath, 13.0f, &config, ranges) != nullptr)
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
