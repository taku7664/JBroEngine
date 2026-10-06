#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Editor/EditorPanel.h>
#include <JBro/Types/String.h>

#include <imgui.h>

#include <cstdint>

namespace JBro
{
    class EditorApplication;

    // **스프라이트 뷰어 한 장**이다(D-155·D-284, 기존 `CSpriteViewerPanel`). 그림마다 하나씩 서는 비고유 패널이고,
    // 뿌리에 메인 도크와 나란히 붙는 "스프라이트 뷰어" 도크(`SpriteViewerDockArea`)에 속한다 - 기존의 `CSpriteViewerDockWindow`
    // 자리다. 같은 그림을 다시 열면 새로 세우지 않고 그 패널을 앞으로 가져온다(`EditorApplication::OpenSpriteViewer`).
    //
    //   ┌──────────────────────────┬──────────────────┐
    //   │  시트 + 칸 격자            │  미리보기         │
    //   │  (칸 누름 = 칸 고르기)     ├──────────────────┤
    //   │                          │  임포트 옵션      │
    //   └──────────────────────────┴──────────────────┘
    //
    // 임포트 옵션은 **인스펙터와 같은 함수로** 그린다(`InspectorPanel::DrawAssetOptions`).
    // 기존도 `SpriteImportOptionsEditor` 하나를 두 창이 나눠 썼다 - 각자 그리면 한쪽의 편집이
    // 다른 쪽에서 조용히 사라진다. 그래서 앞에 있는 뷰어의 그림이 곧 고른 에셋이다.
    class SpriteViewerPanel final : public InstancePanel
    {
    public:
        static constexpr const char* TypeName = "SpriteViewer";
        // 소속 도크의 이름이다. 뿌리에 메인 도크와 나란히 붙는다.
        static constexpr const char* DockAreaName = "SpriteViewer";
        // 긴 변의 한계. 시트의 칸 경계가 보여야 칸을 고를 수 있다.
        static constexpr std::uint32_t SheetMaxSide = 1024;

        const char* GetTitle() const override;
        // 탭에는 그림의 경로가 보인다.
        const char* GetDisplayTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        // 잡고 있던 스프라이트를 놓는다. 놓지 않으면 닫아도 `CollectUnused` 가 그 그림을 영영 내리지 못한다.
        void OnDestroy() override;
        void OnUpdate(float deltaTime) override;
        void OnDraw() override;

        // Texture 든 Sprite 든 받아 그 그림의 두 레코드를 찾는다. 그림이 아니면 거짓이다.
        static bool ResolvePicture(EditorApplication& editor, AssetId asset, AssetId& texture, AssetId& sprite);
        // 이 패널에 그 그림을 싣는다. 스프라이트를 잡지 못하면 거짓이다. 만든 직후 한 번 부른다.
        bool Show(AssetId texture, AssetId sprite);
        AssetId GetTexture() const { return m_texture; }
        // 그 그림을 고른 에셋으로 삼는다. 프레임 고르기 중이면 하지 않는다.
        void SelectPicture();

        std::uint32_t GetFrame() const { return m_frame; }
        // 마우스가 가리킨 칸이다. 가리킨 것이 없으면 -1 이다(D-185).
        int GetHoveredFrame() const { return m_hoveredFrame; }

    private:
        void DrawSheet(const ImVec2& area);
        void DrawPreview(float deltaTime);

        EditorApplication* m_editor = nullptr;
        AssetId m_texture;
        AssetId m_sprite;
        AssetHandle m_spriteHandle;
        String m_name;
        std::uint32_t m_frame = 0;
        bool m_playing = false;
        float m_framesPerSecond = 12.0f;
        float m_clock = 0.0f;
        // **시트의 배율**(D-185, 기존 `확대` 슬라이더와 `창에 맞추기`). 0 이면 칸에 맞춘다 -
        // 처음 열었을 때 시트 전체가 보여야 어디를 볼지 고를 수 있고, 그 뒤로 사람이 키우면
        // 그 값을 지킨다. 창 크기가 바뀌어도 사람이 고른 배율은 따라 움직이지 않는다.
        float m_sheetZoom = 0.0f;
        // 미리보기에 피벗을 십자로 그릴지(기존 `피벗 표시`). 그림이 어느 점을 기준으로
        // 놓이는지는 눈으로 봐야 안다.
        bool m_showPivot = false;
        // 마우스가 가리킨 칸이다. 없으면 -1 이다. 시트를 그리며 정하고 그 아래에 적는다.
        int m_hoveredFrame = -1;
        // 왼쪽 시트 칸의 폭. 사람이 끌어 바꾼다.
        float m_sheetWidth = 0.0f;
        // 지난 프레임에 앞에 있었는가. 앞으로 오는 순간 그 그림을 고른다 - 옵션 칸이 그 그림의 것이어야 한다.
        bool m_wasVisible = false;
    };
}
