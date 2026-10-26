#pragma once

#include <JBro/Editor/EditorPanel.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // **렌더 그래프 창**(D-340, postprocess-plan §2.6). 지난 프레임에 렌더러가 지은 그래프를 보여 준다 - 어느 패스가 돌았고 어느 패스가
    // 왜 건너뛰었는지, 텍스처가 어느 패스부터 어느 패스까지 살고 무엇과 실제 텍스처를 나눠 쓰는지, 패스 앞에 무슨 배리어가 걸렸는지,
    // 풀에 무엇이 있고 다음 프레임에 무엇을 만들려는지다. 레이어 블렌드가 한 프레임 늦게 걸리거나 그림자가 안 나올 때 여는 창이다.
    //
    // **그래프는 프레임마다 바뀐다.** 창은 매 프레임 렌더러의 그래프를 제 저장소로 떠 오고, 멈추면 떠 둔 것을 그대로 보인다.
    // 렌더러를 붙잡지 않으므로 멈춰도 게임은 돈다. 창을 닫아 두면 뜨지 않는다.
    class RenderGraphPanel final : public UniquePanel
    {
    public:
        // 패널 종류 이름이다(D-284). 종류 표와 `GetTitle` 이 같은 글자를 쓴다.
        static constexpr const char* TypeName = "RenderGraph";

        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        Bool OnCreate(EditorApplication& editor) override;
        void OnUpdate(Float deltaTime) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Bottom; }

        // ── 시험이 보는 것 ──
        Bool IsPaused() const noexcept { return m_paused; }
        void SetPaused(Bool paused) noexcept { m_paused = paused; }
        // 떠 둔 그래프의 프레임 번호다(`Renderer::GetLastRenderGraphSerial`). 떠 둔 것이 없으면 0 이다.
        UInt64 GetCapturedSerial() const noexcept { return m_serial; }
        UInt32 GetCapturedPassCount() const noexcept { return static_cast<UInt32>(m_passes.Size()); }
        UInt32 GetCapturedTextureCount() const noexcept { return static_cast<UInt32>(m_textures.Size()); }
        // 그 이름의 패스 가운데 건너뛴 까닭이 `skip` 인 것의 수다.
        UInt32 CountPasses(const char* name, RenderGraphSkip skip) const noexcept;
        // 떠 둔 그래프에서 실제 텍스처를 나눠 쓴 그래프 텍스처 묶음의 수(둘 이상이 한 텍스처를 쓴 것)다.
        UInt32 CountSharedTextures() const noexcept;
        void SelectPass(UInt32 index) noexcept { m_selectedPass = index; }
        // 찾기 칸의 글자다. 패스 이름이나 그 패스가 쓰는(조건으로만 가리키는 것도) 텍스처 이름에 들어 있으면 보인다.
        void SetFilter(const char* text) { m_filter = text != nullptr ? text : ""; }
        Bool IsPassShownByFilter(UInt32 index) const noexcept
        {
            return index < m_passes.Size() && PassMatchesFilter(m_passes[index.Get()]);
        }
        UInt32 GetSelectedPass() const noexcept { return m_selectedPass; }
        // 오른쪽 탭이다. 다음 그리기에서 그 탭을 앞으로 꺼낸다.
        enum class Tab : std::uint8_t
        {
            Pass,
            Textures,
            Pool
        };
        void ShowTab(Tab tab) noexcept
        {
            m_requestedTab = tab;
            m_tabRequested = true;
        }
        // 그 이름의 첫 패스 번호다. 없으면 `NoSelection` 이다.
        UInt32 FindPass(const char* name) const noexcept;
        static constexpr UInt32 NoSelection = 0xFFFFFFFFu;

    private:
        enum class ViewRole : std::uint8_t
        {
            // 프레임 타깃으로 간 뷰 - 에디터에서는 게임 화면이 그린 곳이다.
            Frame,
            Canvas,
            Simulation,
            // 그 밖에 자기 타깃을 든 뷰(레이어 썸네일 같은 것).
            Other
        };
        struct ViewRow
        {
            RendererViewDebugInfo info;
            ViewRole role = ViewRole::Frame;
        };
        struct PassRow
        {
            const char* name = nullptr;
            RenderGraphPassKind kind = RenderGraphPassKind::Raster;
            RenderGraphSkip skip = RenderGraphSkip::None;
            UInt32 group = 0;
            UInt32 firstAccess = 0;
            UInt32 accessCount = 0;
            UInt32 firstBarrier = 0;
            UInt32 barrierCount = 0;
            UInt32 condition = RenderGraphTexture::Invalid;
            Bool runIfMissing = false;
        };
        struct AccessRow
        {
            UInt32 texture = RenderGraphTexture::Invalid;
            RenderGraphAccess access = RenderGraphAccess::Read;
            LoadOperation load = LoadOperation::Load;
            Bool optional = false;
        };
        struct TextureRow
        {
            const char* name = nullptr;
            RenderGraphTextureKind kind = RenderGraphTextureKind::Transient;
            TransientTextureDesc desc;
            TextureHandle physical;
            UInt32 firstPass = 0;
            UInt32 lastPass = 0;
            Bool used = false;
            // 같은 실제 텍스처를 쓰는 묶음의 번호다. 막대 색이 이것으로 갈린다. 실제 텍스처가 없으면 `NoShare` 다.
            UInt32 share = 0;
        };
        struct BarrierRow
        {
            // 그 패스의 접근에서 찾은 그래프 텍스처다. 못 찾으면 무효다.
            UInt32 texture = RenderGraphTexture::Invalid;
            TextureAccess access = TextureAccess::ShaderRead;
        };
        struct SlotRow
        {
            Bool occupied = false;
            TransientTextureDesc desc;
            TextureHandle texture;
            UInt32 idleFrames = 0;
            // 떠 둔 그래프가 이 칸을 썼는가.
            Bool usedByGraph = false;
        };
        struct WantRow
        {
            TransientTextureDesc desc;
            UInt32 count = 0;
        };
        static constexpr UInt32 NoShare = 0xFFFFFFFFu;

        void Capture(const Renderer& renderer);
        void DrawToolBar();
        void DrawPassList();
        void DrawPassDetail();
        void DrawTextures();
        void DrawPool();
        Bool PassMatchesFilter(const PassRow& pass) const;
        Bool PassTouchesTexture(const PassRow& pass, UInt32 texture) const;
        const char* TextureLabel(UInt32 texture) const;
        const char* ViewLabel(const ViewRow& view) const;

        EditorApplication* m_editor = nullptr;
        Array<ViewRow> m_views;
        Array<PassRow> m_passes;
        Array<AccessRow> m_accesses;
        Array<TextureRow> m_textures;
        Array<BarrierRow> m_barriers;
        SlotRow m_slots[RenderTargetPool::Capacity];
        Array<WantRow> m_wants;
        UInt64 m_serial = 0;
        UInt32 m_ranCount = 0;
        UInt32 m_physicalCount = 0;
        UInt32 m_barrierTotal = 0;
        UInt32 m_slotsUsed = 0;
        Bool m_paused = false;
        // 멈춘 채로 한 프레임만 받는다.
        Bool m_stepOnce = false;
        Bool m_showSkipped = true;
        String m_filter;
        UInt32 m_selectedPass = NoSelection;
        UInt32 m_selectedTexture = NoSelection;
        Float m_listWidth = 460.0f;
        Tab m_requestedTab = Tab::Pass;
        Bool m_tabRequested = false;
    };
}
