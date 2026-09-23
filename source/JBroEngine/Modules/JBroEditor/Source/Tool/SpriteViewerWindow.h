#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <imgui.h>

#include <cstdint>

namespace JBro
{
    class EditorApplication;

    // **스프라이트 뷰어**다(D-155, 기존 `CSpriteViewerDockWindow` + `CSpriteViewerPanel`).
    //
    // 기존 엔진의 두 겹 도크에서 **메인 도크와 나란히 뿌리에 붙는 창**이었다 - 도구 창(패널)은
    // 메인 도크 안에 붙고, 파일을 여는 창(임포터·뷰어)은 뿌리에 붙어 메인 도크와 탭으로 선다.
    // 파일마다 탭이 하나다.
    //
    //   ┌──────────────────────────┬──────────────────┐
    //   │  시트 + 칸 격자            │  미리보기         │
    //   │  (칸 누름 = 칸 고르기)     ├──────────────────┤
    //   │                          │  임포트 옵션      │
    //   └──────────────────────────┴──────────────────┘
    //
    // 임포트 옵션은 **인스펙터와 같은 함수로** 그린다(`InspectorPanel::DrawAssetOptions`).
    // 기존도 `SpriteImportOptionsEditor` 하나를 두 창이 나눠 썼다 - 각자 그리면 한쪽의 편집이
    // 다른 쪽에서 조용히 사라진다. 그래서 앞에 있는 탭의 에셋이 곧 고른 에셋이다.
    class SpriteViewerWindow
    {
    public:
        // 긴 변의 한계. 시트의 칸 경계가 보여야 칸을 고를 수 있다.
        static constexpr std::uint32_t SheetMaxSide = 1024;

        void Initialize(EditorApplication& editor);
        void Shutdown();

        // 이 그림을 탭으로 연다. 이미 열려 있으면 그 탭을 앞으로 꺼낸다. 그림이 아니면 거짓이다.
        // Texture 든 Sprite 든 받는다 - 짝을 찾아 둘 다 든다.
        bool Open(AssetId asset);
        // 뿌리 도크 노드 안에 그린다. 연 탭이 없으면 창도 없다.
        void Draw(ImGuiID rootDock, const ImGuiWindowClass& rootClass);
        // 프로젝트를 닫을 때. 잡고 있던 스프라이트를 놓는다.
        void Clear();

        std::size_t GetTabCount() const { return m_tabs.Size(); }
        // 앞에 있는 탭의 칸 번호다. 탭이 없으면 거짓이다. 테스트와 진단이 쓴다.
        bool GetActiveFrame(std::uint32_t& frame) const;
        // 마우스가 가리킨 칸이다. 가리킨 것이 없으면 -1 이고, 탭이 없어도 -1 이다(D-185).
        int GetHoveredFrame() const;

    private:
        struct Tab
        {
            AssetId texture;
            AssetId sprite;
            AssetHandle spriteHandle;
            String name;
            std::uint32_t frame = 0;
            bool playing = false;
            float framesPerSecond = 12.0f;
            float clock = 0.0f;
            bool open = true;
            // **시트의 배율**(D-185, 기존 `확대` 슬라이더와 `창에 맞추기`). 0 이면 칸에 맞춘다 -
            // 처음 열었을 때 시트 전체가 보여야 어디를 볼지 고를 수 있고, 그 뒤로 사람이 키우면
            // 그 값을 지킨다. 창 크기가 바뀌어도 사람이 고른 배율은 따라 움직이지 않는다.
            float sheetZoom = 0.0f;
            // 미리보기에 피벗을 십자로 그릴지(기존 `피벗 표시`). 그림이 어느 점을 기준으로
            // 놓이는지는 눈으로 봐야 안다.
            bool showPivot = false;
            // 마우스가 가리킨 칸이다. 없으면 -1 이다. 시트를 그리며 정하고 그 아래에 적는다.
            int hoveredFrame = -1;
        };

        void DrawTab(Tab& tab, float deltaTime);
        void DrawSheet(Tab& tab, const ImVec2& area);
        void DrawPreview(Tab& tab, float deltaTime);
        void CloseTab(std::size_t index);
        // 그 그림을 고른 에셋으로 삼는다. 프레임 고르기 중이면 하지 않는다.
        void SelectPicture(AssetId texture);

        EditorApplication* m_editor = nullptr;
        Array<Tab> m_tabs;
        // 앞으로 꺼낼 탭. 연 순간 한 번만 앞으로 온다 - 매 프레임 꺼내면 다른 탭을 누를 수 없다.
        std::size_t m_selectNext = static_cast<std::size_t>(-1);
        std::size_t m_active = static_cast<std::size_t>(-1);
        // 처음 뜰 때 뿌리 노드에 붙인다. 그 뒤로는 사람이 옮긴 자리를 지킨다.
        bool m_dockNext = true;
        // 왼쪽 시트 칸의 폭(비율). 사람이 끌어 바꾼다.
        float m_sheetWidth = 0.0f;
    };
}
