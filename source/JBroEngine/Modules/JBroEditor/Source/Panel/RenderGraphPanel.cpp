#include "RenderGraphPanel.h"

#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/SpanBar.h>

#include <imgui.h>

#include <cctype>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/ValueMath.h>

namespace JBro
{
    namespace
    {
        // 포맷 이름은 RHI 의 이름 그대로다 - 엔진 식별자라 번역하지 않는다(컴포넌트 타입 이름처럼).
        const char* FormatName(TextureFormat format)
        {
            switch (format)
            {
            case TextureFormat::Unknown:
                return "-";
            case TextureFormat::RGBA8Unorm:
                return "RGBA8Unorm";
            case TextureFormat::RGBA8UnormSrgb:
                return "RGBA8UnormSrgb";
            case TextureFormat::BGRA8Unorm:
                return "BGRA8Unorm";
            case TextureFormat::BGRA8UnormSrgb:
                return "BGRA8UnormSrgb";
            case TextureFormat::RGBA16Float:
                return "RGBA16Float";
            case TextureFormat::D32Float:
                return "D32Float";
            case TextureFormat::R11G11B10Float:
                return "R11G11B10Float";
            case TextureFormat::R16Float:
                return "R16Float";
            case TextureFormat::R32Float:
                return "R32Float";
            case TextureFormat::RG16Float:
                return "RG16Float";
            case TextureFormat::RGBA32Float:
                return "RGBA32Float";
            }
            return "-";
        }

        const char* KindLabel(RenderGraphPassKind kind)
        {
            switch (kind)
            {
            case RenderGraphPassKind::Raster:
                return Loc::TextOr(LocKeys::RenderGraphKindRaster, "Raster");
            case RenderGraphPassKind::Copy:
                return Loc::TextOr(LocKeys::RenderGraphKindCopy, "Copy");
            case RenderGraphPassKind::Compute:
                return Loc::TextOr(LocKeys::RenderGraphKindCompute, "Compute");
            }
            return "";
        }

        const char* SkipLabel(RenderGraphSkip skip)
        {
            switch (skip)
            {
            case RenderGraphSkip::None:
                return Loc::TextOr(LocKeys::RenderGraphStatusRan, "Ran");
            case RenderGraphSkip::MissingTexture:
                return Loc::TextOr(LocKeys::RenderGraphSkipMissing, "Texture missing");
            case RenderGraphSkip::TexturePresent:
                return Loc::TextOr(LocKeys::RenderGraphSkipPresent, "Not needed");
            case RenderGraphSkip::InputNotWritten:
                return Loc::TextOr(LocKeys::RenderGraphSkipInput, "No input");
            case RenderGraphSkip::Unused:
                return Loc::TextOr(LocKeys::RenderGraphSkipUnused, "Unused");
            }
            return "";
        }

        // 왜 건너뛰었는지를 문장으로 말한다. 짧은 이름만으로는 무엇을 해야 하는지 모른다.
        const char* SkipReason(RenderGraphSkip skip)
        {
            switch (skip)
            {
            case RenderGraphSkip::None:
                return Loc::TextOr(LocKeys::RenderGraphStatusRanTip, "This pass was recorded this frame.");
            case RenderGraphSkip::MissingTexture:
                return Loc::TextOr(LocKeys::RenderGraphSkipMissingTip,
                    "A texture this pass needs does not exist this frame. Its shape is made before the next frame.");
            case RenderGraphSkip::TexturePresent:
                return Loc::TextOr(LocKeys::RenderGraphSkipPresentTip,
                    "This is a fallback. The texture exists, so the original pass ran instead.");
            case RenderGraphSkip::InputNotWritten:
                return Loc::TextOr(LocKeys::RenderGraphSkipInputTip,
                    "The pass that writes a texture this pass must read did not run.");
            case RenderGraphSkip::Unused:
                return Loc::TextOr(LocKeys::RenderGraphSkipUnusedTip, "Nothing reads what this pass writes, so it was dropped.");
            }
            return "";
        }

        ImVec4 SkipColor(RenderGraphSkip skip)
        {
            // 돈 패스는 본문 색, 텍스처가 없어 못 돈 것은 경고 색(다음 프레임에 풀린다), 나머지는 흐린 색이다.
            switch (skip)
            {
            case RenderGraphSkip::None:
                return EditorTheme::Ink;
            case RenderGraphSkip::MissingTexture:
                return EditorTheme::Amber;
            default:
                return EditorTheme::InkMuted;
            }
        }

        const char* AccessLabel(RenderGraphAccess access, Bool optional)
        {
            switch (access)
            {
            case RenderGraphAccess::Color:
                return Loc::TextOr(LocKeys::RenderGraphAccessColor, "Colour attachment");
            case RenderGraphAccess::Depth:
                return Loc::TextOr(LocKeys::RenderGraphAccessDepth, "Depth attachment");
            case RenderGraphAccess::Read:
                return optional ? Loc::TextOr(LocKeys::RenderGraphAccessReadOptional, "Read (may be absent)")
                                : Loc::TextOr(LocKeys::RenderGraphAccessRead, "Read");
            case RenderGraphAccess::CopySource:
                return Loc::TextOr(LocKeys::RenderGraphAccessCopySource, "Copy source");
            case RenderGraphAccess::CopyDestination:
                return Loc::TextOr(LocKeys::RenderGraphAccessCopyDestination, "Copy destination");
            case RenderGraphAccess::Storage:
                return Loc::TextOr(LocKeys::RenderGraphAccessStorage, "Read and write");
            }
            return "";
        }

        const char* LoadLabel(LoadOperation load)
        {
            switch (load)
            {
            case LoadOperation::Load:
                return Loc::TextOr(LocKeys::RenderGraphLoadLoad, "Keep");
            case LoadOperation::Clear:
                return Loc::TextOr(LocKeys::RenderGraphLoadClear, "Clear");
            case LoadOperation::Discard:
                return Loc::TextOr(LocKeys::RenderGraphLoadDiscard, "Discard");
            }
            return "";
        }

        const char* StateLabel(TextureAccess access)
        {
            switch (access)
            {
            case TextureAccess::ShaderRead:
                return Loc::TextOr(LocKeys::RenderGraphStateShaderRead, "Shader read");
            case TextureAccess::RenderTarget:
                return Loc::TextOr(LocKeys::RenderGraphStateRenderTarget, "Render target");
            case TextureAccess::DepthWrite:
                return Loc::TextOr(LocKeys::RenderGraphStateDepthWrite, "Depth write");
            case TextureAccess::CopySource:
                return Loc::TextOr(LocKeys::RenderGraphStateCopySource, "Copy source");
            case TextureAccess::CopyDestination:
                return Loc::TextOr(LocKeys::RenderGraphStateCopyDestination, "Copy destination");
            case TextureAccess::Storage:
                return Loc::TextOr(LocKeys::RenderGraphStateStorage, "Read and write");
            }
            return "";
        }

        const char* SourceLabel(RenderGraphTextureKind kind)
        {
            switch (kind)
            {
            case RenderGraphTextureKind::Transient:
                return Loc::TextOr(LocKeys::RenderGraphSourceTransient, "Graph");
            case RenderGraphTextureKind::Imported:
                return Loc::TextOr(LocKeys::RenderGraphSourceImported, "Imported");
            case RenderGraphTextureKind::BackBuffer:
                return Loc::TextOr(LocKeys::RenderGraphSourceBackBuffer, "Back buffer");
            }
            return "";
        }

        // 나눠 쓰는 묶음마다 다른 색이다. 같은 색 막대는 실제 텍스처 하나를 나눠 쓴다.
        ImVec4 ShareColor(UInt32 share)
        {
            static const ImVec4 colors[] = {
                ImVec4(0.231f, 0.510f, 0.965f, 1.0f),
                ImVec4(0.322f, 0.780f, 0.478f, 1.0f),
                ImVec4(0.878f, 0.639f, 0.235f, 1.0f),
                ImVec4(0.690f, 0.443f, 0.910f, 1.0f),
                ImVec4(0.290f, 0.776f, 0.808f, 1.0f),
                ImVec4(0.878f, 0.353f, 0.322f, 1.0f),
            };
            constexpr UInt32 count = static_cast<UInt32>(sizeof(colors) / sizeof(colors[0]));
            return colors[(share % count).Get()];
        }

        Bool ContainsFold(const char* text, const String& needle)
        {
            if (needle.empty())
            {
                return true;
            }
            if (text == nullptr)
            {
                return false;
            }
            for (const char* start = text; *start != '\0'; ++start)
            {
                std::size_t at = 0;
                while (at < needle.size() && start[at] != '\0'
                    && std::tolower(static_cast<unsigned char>(start[at])) == std::tolower(static_cast<unsigned char>(needle[at])))
                {
                    ++at;
                }
                if (at == needle.size())
                {
                    return true;
                }
            }
            return false;
        }

        void ShapeText(const TransientTextureDesc& desc)
        {
            if (desc.format == TextureFormat::Unknown)
            {
                Widget::HintText("-");
                return;
            }
            Widget::TextF("%u x %u %s", desc.extent.width, desc.extent.height, FormatName(desc.format));
        }
    }

    const char* RenderGraphPanel::GetTitle() const
    {
        return TypeName;
    }

    const char* RenderGraphPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelRenderGraph, "Render Graph");
    }

    Bool RenderGraphPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        // 늘 보는 창이 아니다. 디버그 메뉴에서 열어 본다.
        SetOpen(false);
        return true;
    }

    void RenderGraphPanel::OnDestroy()
    {
        if (m_editor == nullptr || m_editor->GetRenderer() == nullptr)
        {
            return;
        }
        // 창이 사라지면 재는 것과 뜨는 것도 끈다. 둘 다 프레임에 얹히는 일이다.
        m_editor->GetRenderer()->SetRenderGraphTimingEnabled(false);
        m_editor->GetRenderer()->ClearRenderGraphCapture();
    }

    void RenderGraphPanel::OnUpdate(Float deltaTime)
    {
        (void)deltaTime;
        if (m_editor == nullptr)
        {
            return;
        }
        Renderer* renderer = m_editor->GetRenderer();
        if (renderer == nullptr)
        {
            return;
        }
        // **창이 보일 때만 뜨고 잰다**(D-341, 프로파일러와 같다). 닫아 둔 창을 위해 패스마다 시각을 적을 까닭이 없다.
        if (IsOpen() != m_timingOn)
        {
            m_timingOn = IsOpen();
            renderer->SetRenderGraphTimingEnabled(m_timingOn);
            if (false == m_timingOn)
            {
                renderer->ClearRenderGraphCapture();
                m_previewStatus = RenderGraphCaptureStatus::None;
            }
        }
        if (false == IsOpen())
        {
            return;
        }
        m_timingSupported = renderer->IsRenderGraphTimingSupported();
        if (false == m_paused || m_stepOnce)
        {
            const UInt64 before = m_serial;
            Capture(*renderer);
            // 한 프레임 받기는 새 그래프를 받았을 때 끝난다 - 아직 다음 프레임이 기록되지 않았으면 다음 갱신에서 받는다.
            if (m_serial != before)
            {
                m_stepOnce = false;
            }
        }
        // 멈춰 있어도 시간과 미리보기는 따라온다 - 시간은 늦게 오고, 미리보기는 지금 프레임의 내용이다.
        ApplyTimings(*renderer);
        UpdatePreview(*renderer);
    }

    void RenderGraphPanel::ApplyTimings(const Renderer& renderer)
    {
        // 새 시간이 왔거나 다른 그래프를 떠 왔을 때만 다시 붙인다.
        const UInt64 serial = renderer.GetRenderGraphTimingSerial();
        if (serial == 0 || (serial == m_timingSerial && m_serial == m_timingAppliedTo))
        {
            return;
        }
        m_timingSerial = serial;
        m_timingAppliedTo = m_serial;
        m_gpuMilliseconds = renderer.GetRenderGraphGpuMilliseconds();
        for (PassRow& pass : m_passes)
        {
            pass.gpuMilliseconds = -1.0f;
        }
        // 시간이 온 그래프가 떠 둔 그래프이면 패스 번호로, 아니면 뷰·이름·차례로 짝짓는다 - 그래프의 짜임은 프레임마다 거의 같다.
        const Bool sameGraph = serial == m_serial;
        for (UInt32 index = 0; index < renderer.GetRenderGraphTimingCount(); ++index)
        {
            RenderGraphPassTiming timing;
            renderer.GetRenderGraphTiming(index, timing);
            if (sameGraph)
            {
                if (timing.pass < m_passes.Size())
                {
                    m_passes[timing.pass.Get()].gpuMilliseconds = timing.milliseconds;
                }
                continue;
            }
            // 이 시간이 그 뷰에서 그 이름의 몇 번째인지 센다.
            UInt32 rank = 0;
            for (UInt32 earlier = 0; earlier < index; ++earlier)
            {
                RenderGraphPassTiming previous;
                renderer.GetRenderGraphTiming(earlier, previous);
                if (previous.group == timing.group && previous.name == timing.name)
                {
                    ++rank;
                }
            }
            for (PassRow& pass : m_passes)
            {
                if (pass.group == timing.group && pass.name == timing.name && pass.occurrence == rank)
                {
                    pass.gpuMilliseconds = timing.milliseconds;
                    break;
                }
            }
        }
    }

    void RenderGraphPanel::UpdatePreview(Renderer& renderer)
    {
        if (m_previewPass == NoSelection || m_previewPass >= m_passes.Size())
        {
            if (m_previewStatus != RenderGraphCaptureStatus::None)
            {
                renderer.ClearRenderGraphCapture();
                m_previewStatus = RenderGraphCaptureStatus::None;
            }
            return;
        }
        const PassRow& pass = m_passes[m_previewPass.Get()];
        RenderGraphCaptureRequest request;
        request.passName = pass.name;
        request.group = pass.group;
        request.occurrence = pass.occurrence;
        request.access = m_previewAccess;
        renderer.RequestRenderGraphCapture(request);
        m_previewStatus = renderer.GetRenderGraphCaptureStatus();
        m_previewTexture = renderer.GetRenderGraphCaptureTexture();
        m_previewExtent = renderer.GetRenderGraphCaptureExtent();
        m_previewSerial = renderer.GetRenderGraphCaptureSerial();
    }

    void RenderGraphPanel::Capture(const Renderer& renderer)
    {
        const RenderGraph* graph = renderer.GetLastRenderGraph();
        const UInt64 serial = renderer.GetLastRenderGraphSerial();
        if (graph == nullptr || serial == m_serial)
        {
            return;
        }
        m_serial = serial;

        const TextureHandle canvas = m_editor->GetCanvasViewTexture();
        const TextureHandle simulation = m_editor->GetSimulationViewTexture();
        m_views.Clear();
        for (UInt32 index = 0; index < renderer.GetLastRenderGraphViewCount(); ++index)
        {
            ViewRow row;
            renderer.GetLastRenderGraphView(index, row.info);
            if (simulation.IsValid() && row.info.target == simulation)
            {
                row.role = ViewRole::Simulation;
            }
            else if (canvas.IsValid() && row.info.target == canvas)
            {
                row.role = ViewRole::Canvas;
            }
            else
            {
                row.role = row.info.ownTarget ? ViewRole::Other : ViewRole::Frame;
            }
            m_views.Add(row);
        }

        // 텍스처와 나눠 쓰는 묶음이다. 그래프가 만든 텍스처만 묶는다 - 들인 텍스처는 제 핸들 하나다.
        m_textures.Clear();
        m_physicalCount = 0;
        for (UInt32 index = 0; index < graph->GetTextureCount(); ++index)
        {
            const RenderGraphTexture texture{index};
            TextureRow row;
            row.name = graph->GetTextureName(texture);
            row.kind = graph->GetTextureKind(texture);
            row.desc = graph->GetTextureDesc(texture);
            row.physical = graph->GetCompiledTexture(texture);
            row.used = graph->GetTextureLifetime(texture, row.firstPass, row.lastPass);
            row.share = NoShare;
            if (row.kind == RenderGraphTextureKind::Transient && row.physical.IsValid())
            {
                for (std::size_t earlier = 0; earlier < m_textures.Size(); ++earlier)
                {
                    if (m_textures[earlier].kind == RenderGraphTextureKind::Transient && m_textures[earlier].physical == row.physical)
                    {
                        row.share = m_textures[earlier].share;
                        break;
                    }
                }
                if (row.share == NoShare)
                {
                    row.share = m_physicalCount;
                    ++m_physicalCount;
                }
            }
            m_textures.Add(row);
        }

        m_passes.Clear();
        m_accesses.Clear();
        m_barriers.Clear();
        m_ranCount = 0;
        m_barrierTotal = 0;
        for (UInt32 index = 0; index < graph->GetPassCount(); ++index)
        {
            const RenderGraphPass pass{index};
            PassRow row;
            row.name = graph->GetPassName(pass);
            row.kind = graph->GetPassKind(pass);
            row.skip = graph->GetPassSkip(pass);
            row.group = graph->GetPassGroup(pass);
            row.condition = graph->GetPassCondition(pass, row.runIfMissing).index;
            row.firstAccess = static_cast<UInt32>(m_accesses.Size());
            row.accessCount = graph->GetPassAccessCount(pass);
            for (UInt32 at = 0; at < row.accessCount; ++at)
            {
                const RenderGraphAccessInfo info = graph->GetPassAccess(pass, at);
                m_accesses.Add({info.texture.index, info.access, info.load, info.optional});
            }
            // 배리어는 실제 텍스처로 적혀 있다. 이 패스가 쓰는 그래프 텍스처 가운데 같은 실제 텍스처를 찾아 이름을 붙인다.
            const JArrayView<TextureBarrier> barriers = graph->GetPassBarriers(pass);
            row.firstBarrier = static_cast<UInt32>(m_barriers.Size());
            row.barrierCount = barriers.size;
            for (UInt32 at = 0; at < barriers.size; ++at)
            {
                BarrierRow barrier;
                barrier.access = barriers.data[at.Get()].access;
                for (UInt32 access = 0; access < row.accessCount; ++access)
                {
                    const UInt32 texture = m_accesses[(row.firstAccess + access).Get()].texture;
                    if (texture < m_textures.Size() && m_textures[texture.Get()].physical == barriers.data[at.Get()].texture)
                    {
                        barrier.texture = texture;
                        break;
                    }
                }
                m_barriers.Add(barrier);
            }
            if (row.skip == RenderGraphSkip::None)
            {
                ++m_ranCount;
                m_barrierTotal += row.barrierCount;
            }
            for (const PassRow& earlier : m_passes)
            {
                if (earlier.group == row.group && earlier.name == row.name)
                {
                    ++row.occurrence;
                }
            }
            m_passes.Add(row);
        }

        const RenderTargetPool& pool = renderer.GetRenderTargetPool();
        m_slotsUsed = 0;
        for (UInt32 slot = 0; slot < RenderTargetPool::Capacity; ++slot)
        {
            SlotRow& row = m_slots[slot.Get()];
            row = {};
            row.occupied = pool.GetSlot(slot, row.desc, row.texture, row.idleFrames);
            if (false == row.occupied)
            {
                continue;
            }
            ++m_slotsUsed;
            for (const TextureRow& texture : m_textures)
            {
                if (texture.kind == RenderGraphTextureKind::Transient && texture.physical == row.texture)
                {
                    row.usedByGraph = true;
                    break;
                }
            }
        }
        m_wants.Clear();
        for (UInt32 want = 0; want < pool.GetWantCount(); ++want)
        {
            WantRow row;
            if (pool.GetWant(want, row.desc, row.count))
            {
                m_wants.Add(row);
            }
        }

        if (m_selectedPass != NoSelection && m_selectedPass >= m_passes.Size())
        {
            m_selectedPass = NoSelection;
        }
        if (m_previewPass != NoSelection && m_previewPass >= m_passes.Size())
        {
            m_previewPass = NoSelection;
        }
        if (m_selectedTexture != NoSelection && m_selectedTexture >= m_textures.Size())
        {
            m_selectedTexture = NoSelection;
        }
    }

    void RenderGraphPanel::OnDraw()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        DrawToolBar();
        ImGui::Separator();
        if (m_serial == 0)
        {
            Widget::HintText(Loc::TextOr(LocKeys::RenderGraphEmpty, "No render graph has been recorded yet"));
            return;
        }

        const Float available = ImGui::GetContentRegionAvail().x;
        if (ImGui::BeginChild("##render_graph_passes", ImVec2(m_listWidth, 0.0f), ImGuiChildFlags_Borders))
        {
            DrawPassList();
        }
        ImGui::EndChild();
        ImGui::SameLine(0.0f, 0.0f);
        Widget::Splitter("##render_graph_split", true, 4.0f, &m_listWidth, 200.0f, JBro::Max(240.0f, available - 240.0f));
        ImGui::SameLine(0.0f, 0.0f);
        if (ImGui::BeginChild("##render_graph_detail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
        {
            if (Widget::BeginTabs("##render_graph_tabs"))
            {
                const Bool request = m_tabRequested;
                m_tabRequested = false;
                if (Widget::BeginTab(Loc::TextOr(LocKeys::RenderGraphTabPass, "Pass"), nullptr, request && m_requestedTab == Tab::Pass))
                {
                    DrawPassDetail();
                    Widget::EndTab();
                }
                if (Widget::BeginTab(Loc::TextOr(LocKeys::RenderGraphTabTextures, "Textures"), nullptr, request && m_requestedTab == Tab::Textures))
                {
                    DrawTextures();
                    Widget::EndTab();
                }
                if (Widget::BeginTab(Loc::TextOr(LocKeys::RenderGraphTabPool, "Pool"), nullptr, request && m_requestedTab == Tab::Pool))
                {
                    DrawPool();
                    Widget::EndTab();
                }
                Widget::EndTabs();
            }
        }
        ImGui::EndChild();
    }

    void RenderGraphPanel::DrawToolBar()
    {
        // 켜고 끄는 단추는 켜졌을 때 칠한다(D-278). 멈춰도 게임은 돈다 - 창이 떠 둔 그래프를 바꾸지 않을 뿐이다.
        if (Widget::IconButton("##render_graph_pause", Icons::Pause)
                .Selected(m_paused)
                .Tooltip(Loc::TextOr(LocKeys::RenderGraphPauseTooltip, "Hold this graph to inspect it"))
                .Draw())
        {
            m_paused = false == m_paused;
            m_stepOnce = false;
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (Widget::IconButton("##render_graph_step", Icons::StepFrame)
                .Disabled(false == m_paused)
                .Tooltip(m_paused ? Loc::TextOr(LocKeys::RenderGraphStepTooltip, "Take the next frame's graph")
                                  : Loc::TextOr(LocKeys::RenderGraphStepDisabled, "Only while the graph is held"))
                .Draw())
        {
            m_stepOnce = true;
        }
        Widget::ToolBarSeparator();
        // 이름표는 칸 옆 글자다(로그 창과 같다).
        Widget::Checkbox("##render_graph_show_skipped", m_showSkipped);
        ImGui::SameLine(0.0f, 4.0f);
        Widget::Text(Loc::TextOr(LocKeys::RenderGraphShowSkipped, "Skipped passes"));
        ImGui::SameLine(0.0f, 12.0f);
        Widget::SearchBox("##render_graph_filter", m_filter)
            .Hint(Loc::TextOr(LocKeys::RenderGraphSearch, "Find a pass or texture"))
            .Width(160.0f)
            .Draw();
        if (m_serial == 0)
        {
            return;
        }
        Widget::ToolBarSeparator();
        if (m_paused)
        {
            Widget::SeverityTextF(Widget::Severity::Warning, "%s", Loc::TextOr(LocKeys::RenderGraphHeld, "Held"));
            ImGui::SameLine(0.0f, 8.0f);
        }
        Widget::HintTextF("%s %llu", Loc::TextOr(LocKeys::RenderGraphFrame, "Frame"), m_serial);
        ImGui::SameLine(0.0f, 12.0f);
        Widget::HintTextF("%s %u / %u", Loc::TextOr(LocKeys::RenderGraphPassesRan, "Passes run"), m_ranCount,
            static_cast<UInt32>(m_passes.Size()));
        ImGui::SameLine(0.0f, 12.0f);
        Widget::HintTextF("%s %u", Loc::TextOr(LocKeys::RenderGraphTextureCount, "Textures"), static_cast<UInt32>(m_textures.Size()));
        ImGui::SameLine(0.0f, 12.0f);
        Widget::HintTextF("%s %u", Loc::TextOr(LocKeys::RenderGraphPhysicalCount, "Actual textures"), m_physicalCount);
        ImGui::SameLine(0.0f, 12.0f);
        Widget::HintTextF("%s %u", Loc::TextOr(LocKeys::RenderGraphBarrierCount, "Barriers"), m_barrierTotal);
        ImGui::SameLine(0.0f, 12.0f);
        if (false == m_timingSupported)
        {
            Widget::HintText(Loc::TextOr(LocKeys::RenderGraphGpuUnsupported, "No GPU time"));
            Widget::HoveredTooltip(Loc::TextOr(LocKeys::RenderGraphGpuUnsupportedTip, "This device cannot write GPU timestamps."));
        }
        else if (m_timingSerial == 0)
        {
            Widget::HintText(Loc::TextOr(LocKeys::RenderGraphGpuPending, "Measuring GPU time"));
        }
        else
        {
            Widget::HintTextF("GPU %.3f ms", m_gpuMilliseconds);
            Widget::HoveredTooltip(Loc::TextOr(LocKeys::RenderGraphGpuTip,
                "GPU time of the measured passes. Timestamps arrive after the GPU finishes a frame, a few frames late."));
            if (m_timingSerial != m_serial)
            {
                ImGui::SameLine(0.0f, 6.0f);
                Widget::HintTextF("(%s %llu)", Loc::TextOr(LocKeys::RenderGraphFrame, "Frame"), m_timingSerial);
            }
        }
    }

    Bool RenderGraphPanel::PassTouchesTexture(const PassRow& pass, UInt32 texture) const
    {
        if (pass.condition == texture)
        {
            return true;
        }
        for (UInt32 at = 0; at < pass.accessCount; ++at)
        {
            if (m_accesses[(pass.firstAccess + at).Get()].texture == texture)
            {
                return true;
            }
        }
        return false;
    }

    Bool RenderGraphPanel::PassMatchesFilter(const PassRow& pass) const
    {
        if (m_filter.empty() || ContainsFold(pass.name, m_filter))
        {
            return true;
        }
        // 텍스처 이름으로도 찾는다 - "Layer 를 쓰는 패스" 를 찾는 것이 흔한 쓰임이다. 조건으로만 가리키는 텍스처도 본다 -
        // `ViewLayer` 로 찾으면 그것이 없을 때만 도는 `ViewDirect` 도 나와야 한다(Codex 리뷰).
        if (pass.condition < m_textures.Size() && ContainsFold(m_textures[pass.condition.Get()].name, m_filter))
        {
            return true;
        }
        for (UInt32 at = 0; at < pass.accessCount; ++at)
        {
            const UInt32 texture = m_accesses[(pass.firstAccess + at).Get()].texture;
            if (texture < m_textures.Size() && ContainsFold(m_textures[texture.Get()].name, m_filter))
            {
                return true;
            }
        }
        return false;
    }

    const char* RenderGraphPanel::TextureLabel(UInt32 texture) const
    {
        if (texture >= m_textures.Size() || m_textures[texture.Get()].name == nullptr)
        {
            return "?";
        }
        return m_textures[texture.Get()].name;
    }

    const char* RenderGraphPanel::ViewLabel(const ViewRow& view) const
    {
        switch (view.role)
        {
        case ViewRole::Frame:
            return Loc::TextOr(LocKeys::RenderGraphViewFrame, "Game screen");
        case ViewRole::Canvas:
            return Loc::TextOr(LocKeys::PanelCanvasView, "Canvas View");
        case ViewRole::Simulation:
            return Loc::TextOr(LocKeys::PanelSimulation, "Simulation View");
        case ViewRole::Other:
            return Loc::TextOr(LocKeys::RenderGraphViewOther, "Editor image");
        }
        return "";
    }

    void RenderGraphPanel::DrawPassList()
    {
        if (false == ImGui::BeginTable("##render_graph_pass_table", 6,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit))
        {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnPass, "Pass"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnKind, "Kind"), ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnStatus, "Status"), ImGuiTableColumnFlags_WidthFixed, 96.0f);
        ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnBarriers, "Barriers"), ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnGpu, "GPU ms"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        UInt32 shown = 0;
        for (UInt32 viewIndex = 0; viewIndex < m_views.Size(); ++viewIndex)
        {
            const ViewRow& view = m_views[viewIndex.Get()];
            // 뷰마다 머리 줄이다 - 어느 화면을 그리는 패스인지가 먼저 보여야 한다.
            // 이름 열에 어느 화면인지, 상태 열에 크기를 적는다 - 한 칸에 몰면 좁은 열에서 잘린다.
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            {
                Widget::StyleScope style;
                style.PushColor(ImGuiCol_Text, EditorTheme::AccentBright);
                Widget::TextF("%s %u · %s", Loc::TextOr(LocKeys::RenderGraphView, "View"), viewIndex, ViewLabel(view));
            }
            ImGui::TableSetColumnIndex(3);
            Widget::HintTextF("%u x %u", view.info.extent.width, view.info.extent.height);
            if (false == view.info.recorded)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(1);
                Widget::HintText(Loc::TextOr(LocKeys::RenderGraphViewNotRecorded, "Not recorded this frame"));
                continue;
            }
            for (UInt32 index = 0; index < m_passes.Size(); ++index)
            {
                const PassRow& pass = m_passes[index.Get()];
                if (pass.group != viewIndex || (false == m_showSkipped && pass.skip != RenderGraphSkip::None)
                    || false == PassMatchesFilter(pass))
                {
                    continue;
                }
                ++shown;
                Widget::IdScope id(static_cast<Int32>(index.Get()));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (Widget::SelectableRow("##pass_row", m_selectedPass == index,
                        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                {
                    m_selectedPass = index;
                }
                ImGui::SameLine(0.0f, 0.0f);
                Widget::TextF("%u", index);
                ImGui::TableSetColumnIndex(1);
                {
                    // 고른 텍스처를 쓰는 패스는 강조 색이다.
                    const Bool touches = m_selectedTexture != NoSelection && PassTouchesTexture(pass, m_selectedTexture);
                    Widget::StyleScope style;
                    style.PushColor(ImGuiCol_Text, touches ? EditorTheme::AccentBright : SkipColor(pass.skip));
                    Widget::Text(pass.name != nullptr ? pass.name : "?");
                }
                ImGui::TableSetColumnIndex(2);
                Widget::HintText(KindLabel(pass.kind));
                ImGui::TableSetColumnIndex(3);
                {
                    Widget::StyleScope style;
                    style.PushColor(ImGuiCol_Text, SkipColor(pass.skip));
                    Widget::Text(SkipLabel(pass.skip));
                }
                Widget::HoveredTooltip(SkipReason(pass.skip));
                ImGui::TableSetColumnIndex(4);
                if (pass.barrierCount != 0)
                {
                    Widget::TextF("%u", pass.barrierCount);
                }
                else
                {
                    Widget::HintText("-");
                }
                ImGui::TableSetColumnIndex(5);
                if (pass.gpuMilliseconds >= 0.0f)
                {
                    Widget::TextF("%.3f", pass.gpuMilliseconds);
                }
                else
                {
                    Widget::HintText("-");
                }
            }
        }
        ImGui::EndTable();
        if (shown == 0 && false == m_filter.empty())
        {
            Widget::HintText(Loc::TextOr(LocKeys::CommonNoMatches, "no matches"));
        }
    }

    void RenderGraphPanel::DrawPassDetail()
    {
        if (m_selectedPass == NoSelection || m_selectedPass >= m_passes.Size())
        {
            Widget::HintText(Loc::TextOr(LocKeys::RenderGraphSelectPassHint, "Choose a pass on the left"));
            return;
        }
        const PassRow& pass = m_passes[m_selectedPass.Get()];
        Widget::TextF("#%u  %s", m_selectedPass, pass.name != nullptr ? pass.name : "?");
        ImGui::SameLine(0.0f, 8.0f);
        Widget::HintText(KindLabel(pass.kind));
        if (pass.group < m_views.Size())
        {
            const ViewRow& view = m_views[pass.group.Get()];
            Widget::HintTextF("%s %u · %s", Loc::TextOr(LocKeys::RenderGraphView, "View"), pass.group, ViewLabel(view));
        }
        {
            Widget::StyleScope style;
            style.PushColor(ImGuiCol_Text, SkipColor(pass.skip));
            Widget::Text(SkipLabel(pass.skip));
        }
        Widget::WrappedText(SkipReason(pass.skip));
        ImGui::Spacing();

        if (pass.condition < m_textures.Size())
        {
            Widget::TextF("%s: %s %s", Loc::TextOr(LocKeys::RenderGraphCondition, "Condition"), TextureLabel(pass.condition),
                pass.runIfMissing ? Loc::TextOr(LocKeys::RenderGraphConditionMissing, "missing (fallback)")
                                  : Loc::TextOr(LocKeys::RenderGraphConditionAvailable, "available"));
            ImGui::Spacing();
        }

        Widget::Text(Loc::TextOr(LocKeys::RenderGraphAccesses, "Textures this pass uses"));
        if (ImGui::BeginTable("##render_graph_accesses", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnTexture, "Texture"), ImGuiTableColumnFlags_WidthStretch, 0.35f);
            ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnUse, "Use"), ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnActual, "Actual texture"), ImGuiTableColumnFlags_WidthStretch, 0.25f);
            ImGui::TableHeadersRow();
            for (UInt32 at = 0; at < pass.accessCount; ++at)
            {
                const AccessRow& access = m_accesses[(pass.firstAccess + at).Get()];
                Widget::IdScope id(static_cast<Int32>(at.Get()));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const Bool previewing = m_previewPass == m_selectedPass && m_previewAccess == at;
                if (Widget::SelectableRow(TextureLabel(access.texture), previewing, ImGuiSelectableFlags_SpanAllColumns))
                {
                    // 누르면 그 텍스처를 고르고, 이 패스 직후의 내용을 미리 본다. 다시 누르면 미리보기를 끈다.
                    m_selectedTexture = access.texture;
                    if (previewing)
                    {
                        m_previewPass = NoSelection;
                    }
                    else
                    {
                        m_previewPass = m_selectedPass;
                        m_previewAccess = at;
                    }
                }
                ImGui::TableSetColumnIndex(1);
                if (access.access == RenderGraphAccess::Color || access.access == RenderGraphAccess::Depth)
                {
                    Widget::TextF("%s · %s%s", AccessLabel(access.access, access.optional), LoadLabel(access.load),
                        access.optional ? Loc::TextOr(LocKeys::RenderGraphOptionalSuffix, " (may be absent)") : "");
                }
                else
                {
                    Widget::Text(AccessLabel(access.access, access.optional));
                }
                ImGui::TableSetColumnIndex(2);
                const Bool hasActual = access.texture < m_textures.Size() && m_textures[access.texture.Get()].physical.IsValid();
                if (hasActual && m_textures[access.texture.Get()].share != NoShare)
                {
                    Widget::TextF("#%u", m_textures[access.texture.Get()].share);
                }
                else if (hasActual)
                {
                    Widget::HintText(SourceLabel(m_textures[access.texture.Get()].kind));
                }
                else
                {
                    Widget::SeverityTextF(Widget::Severity::Warning, "%s", Loc::TextOr(LocKeys::RenderGraphActualNone, "None this frame"));
                }
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();

        Widget::Text(Loc::TextOr(LocKeys::RenderGraphBarriersHeader, "Barriers before this pass"));
        if (pass.barrierCount == 0)
        {
            Widget::HintText(Loc::TextOr(LocKeys::RenderGraphNoBarriers, "No barriers"));
            ImGui::Spacing();
            DrawPreview();
            return;
        }
        if (ImGui::BeginTable("##render_graph_barriers", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnTexture, "Texture"), ImGuiTableColumnFlags_WidthStretch, 0.5f);
            ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnState, "State"), ImGuiTableColumnFlags_WidthStretch, 0.5f);
            ImGui::TableHeadersRow();
            for (UInt32 at = 0; at < pass.barrierCount; ++at)
            {
                const BarrierRow& barrier = m_barriers[(pass.firstBarrier + at).Get()];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                Widget::Text(TextureLabel(barrier.texture));
                ImGui::TableSetColumnIndex(1);
                Widget::Text(StateLabel(barrier.access));
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        DrawPreview();
    }

    void RenderGraphPanel::DrawPreview()
    {
        Widget::Text(Loc::TextOr(LocKeys::RenderGraphPreview, "Preview"));
        if (m_previewPass != m_selectedPass || m_previewPass == NoSelection)
        {
            Widget::HintText(Loc::TextOr(LocKeys::RenderGraphPreviewHint, "Choose a texture above to see it right after this pass"));
            return;
        }
        switch (m_previewStatus)
        {
        case RenderGraphCaptureStatus::Captured:
            break;
        case RenderGraphCaptureStatus::Unsupported:
            Widget::HintText(Loc::TextOr(LocKeys::RenderGraphPreviewUnsupported, "Depth textures have no preview"));
            return;
        case RenderGraphCaptureStatus::NotFound:
            Widget::HintText(Loc::TextOr(LocKeys::RenderGraphPreviewNotFound, "This pass did not run this frame"));
            return;
        default:
            Widget::HintText(Loc::TextOr(LocKeys::RenderGraphPreviewWaiting, "Making the copy"));
            return;
        }
        if (false == m_previewTexture.IsValid() || m_previewExtent.width == 0)
        {
            Widget::HintText(Loc::TextOr(LocKeys::RenderGraphPreviewWaiting, "Making the copy"));
            return;
        }
        const PassRow& pass = m_passes[m_previewPass.Get()];
        const UInt32 texture = m_previewAccess < pass.accessCount ? m_accesses[(pass.firstAccess + m_previewAccess).Get()].texture
                                                                  : RenderGraphTexture::Invalid;
        // 그 패스가 끝난 직후의 사본이다. 텍스처를 여러 패스가 나눠 쓰므로 프레임 끝의 내용과 다를 수 있다.
        Widget::HintTextF("%s %llu · #%u %s · %s · %u x %u", Loc::TextOr(LocKeys::RenderGraphFrame, "Frame"), m_previewSerial, m_previewPass,
            pass.name != nullptr ? pass.name : "?", TextureLabel(texture), m_previewExtent.width, m_previewExtent.height);
        const Float side = JBro::Max(80.0f, ImGui::GetContentRegionAvail().x);
        // 테두리를 두른다 - 지우기만 한 타깃은 창 바탕과 거의 같은 색이라 그림이 어디까지인지 안 보인다.
        Widget::StyleScope style;
        style.PushVar(ImGuiStyleVar_ImageBorderSize, 1.0f);
        Widget::Image(m_previewTexture, Widget::FitInside(m_previewExtent.width, m_previewExtent.height, ImVec2(side, 240.0f)));
    }

    void RenderGraphPanel::DrawTextures()
    {
        Widget::HintText(Loc::TextOr(LocKeys::RenderGraphSharedHint, "Bars of one colour share one actual texture."));
        if (false == ImGui::BeginTable("##render_graph_textures", 5,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp))
        {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnTexture, "Texture"), ImGuiTableColumnFlags_WidthStretch, 0.2f);
        ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnShape, "Shape"), ImGuiTableColumnFlags_WidthStretch, 0.27f);
        ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnSource, "Source"), ImGuiTableColumnFlags_WidthStretch, 0.09f);
        ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnActual, "Actual texture"), ImGuiTableColumnFlags_WidthStretch, 0.13f);
        ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnLifetime, "Lifetime"), ImGuiTableColumnFlags_WidthStretch, 0.31f);
        ImGui::TableHeadersRow();
        const UInt32 passCount = static_cast<UInt32>(m_passes.Size());
        const UInt32 marker = m_selectedPass != NoSelection ? m_selectedPass : passCount;
        UInt32 shown = 0;
        for (UInt32 index = 0; index < m_textures.Size(); ++index)
        {
            const TextureRow& texture = m_textures[index.Get()];
            if (false == m_filter.empty() && false == ContainsFold(texture.name, m_filter))
            {
                continue;
            }
            ++shown;
            Widget::IdScope id(static_cast<Int32>(index.Get()));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (Widget::SelectableRow("##texture_row", m_selectedTexture == index,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
            {
                m_selectedTexture = m_selectedTexture == index ? NoSelection : index;
            }
            // 같은 이름(뷰마다의 `ViewTarget`·`Layer`)을 가르려고 처음 쓰는 패스의 뷰 번호를 붙인다.
            ImGui::SameLine(0.0f, 0.0f);
            Widget::Text(texture.name != nullptr ? texture.name : "?");
            if (texture.used && texture.firstPass < m_passes.Size())
            {
                ImGui::SameLine(0.0f, 6.0f);
                Widget::HintTextF("%s %u", Loc::TextOr(LocKeys::RenderGraphView, "View"), m_passes[texture.firstPass.Get()].group);
            }
            ImGui::TableSetColumnIndex(1);
            ShapeText(texture.desc);
            ImGui::TableSetColumnIndex(2);
            Widget::HintText(SourceLabel(texture.kind));
            ImGui::TableSetColumnIndex(3);
            if (texture.share != NoShare)
            {
                Widget::StyleScope style;
                style.PushColor(ImGuiCol_Text, ShareColor(texture.share));
                Widget::TextF("#%u", texture.share);
            }
            else if (texture.kind == RenderGraphTextureKind::Transient)
            {
                Widget::SeverityTextF(Widget::Severity::Warning, "%s", Loc::TextOr(LocKeys::RenderGraphActualNone, "None this frame"));
                Widget::HoveredTooltip(Loc::TextOr(LocKeys::RenderGraphActualNoneTip,
                    "The pool had no free texture of this shape. Passes that need it fell back; it is made before the next frame."));
            }
            else
            {
                Widget::HintText("-");
            }
            ImGui::TableSetColumnIndex(4);
            const Bool strong = m_selectedPass != NoSelection && m_selectedPass < passCount
                && PassTouchesTexture(m_passes[m_selectedPass.Get()], index);
            const ImVec4 color = texture.share != NoShare ? ShareColor(texture.share) : EditorTheme::InkMuted;
            if (Widget::SpanBar("##lifetime", texture.used ? texture.firstPass : UInt32(1), texture.used ? texture.lastPass : UInt32(0),
                    passCount, color, marker, strong))
            {
                if (texture.used)
                {
                    Widget::HoveredTooltip(Loc::TextOr(LocKeys::RenderGraphLifetimeTip, "Lives from the first pass that uses it to the last"));
                }
            }
        }
        ImGui::EndTable();
        if (shown == 0 && false == m_filter.empty())
        {
            Widget::HintText(Loc::TextOr(LocKeys::CommonNoMatches, "no matches"));
        }
    }

    void RenderGraphPanel::DrawPool()
    {
        Widget::TextF("%s %u / %u", Loc::TextOr(LocKeys::RenderGraphPoolSlots, "Slots in use"), m_slotsUsed, RenderTargetPool::Capacity);
        Widget::HintText(Loc::TextOr(LocKeys::RenderGraphPoolHint,
            "Textures unused for 300 frames are released. Shapes the graph lacked are made before the next frame."));
        if (ImGui::BeginTable("##render_graph_pool", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnSlot, "Slot"), ImGuiTableColumnFlags_WidthStretch, 0.1f);
            ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnShape, "Shape"), ImGuiTableColumnFlags_WidthStretch, 0.5f);
            ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnThisFrame, "This frame"), ImGuiTableColumnFlags_WidthStretch, 0.2f);
            ImGui::TableSetupColumn(Loc::TextOr(LocKeys::RenderGraphColumnIdle, "Idle frames"), ImGuiTableColumnFlags_WidthStretch, 0.2f);
            ImGui::TableHeadersRow();
            for (UInt32 slot = 0; slot < RenderTargetPool::Capacity; ++slot)
            {
                const SlotRow& row = m_slots[slot.Get()];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                Widget::TextF("%u", slot);
                ImGui::TableSetColumnIndex(1);
                if (false == row.occupied)
                {
                    Widget::HintText(Loc::TextOr(LocKeys::RenderGraphSlotEmpty, "Empty"));
                    continue;
                }
                ShapeText(row.desc);
                ImGui::TableSetColumnIndex(2);
                if (row.usedByGraph)
                {
                    Widget::Text(Loc::TextOr(LocKeys::RenderGraphSlotUsed, "Used"));
                }
                else
                {
                    Widget::HintText("-");
                }
                ImGui::TableSetColumnIndex(3);
                Widget::TextF("%u", row.idleFrames);
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        Widget::Text(Loc::TextOr(LocKeys::RenderGraphWants, "To make before the next frame"));
        if (m_wants.IsEmpty())
        {
            Widget::HintText(Loc::TextOr(LocKeys::RenderGraphWantsNone, "Nothing is wanted"));
            return;
        }
        for (std::size_t index = 0; index < m_wants.Size(); ++index)
        {
            const WantRow& want = m_wants[index];
            Widget::IdScope id(static_cast<Int32>(index));
            ShapeText(want.desc);
            ImGui::SameLine(0.0f, 8.0f);
            Widget::TextF("x %u", want.count);
        }
    }

    UInt32 RenderGraphPanel::CountPasses(const char* name, RenderGraphSkip skip) const noexcept
    {
        UInt32 count = 0;
        for (const PassRow& pass : m_passes)
        {
            if (pass.skip == skip && pass.name != nullptr && name != nullptr && String(pass.name) == name)
            {
                ++count;
            }
        }
        return count;
    }

    UInt32 RenderGraphPanel::FindPass(const char* name) const noexcept
    {
        for (UInt32 index = 0; index < m_passes.Size(); ++index)
        {
            if (m_passes[index.Get()].name != nullptr && name != nullptr && String(m_passes[index.Get()].name) == name)
            {
                return index;
            }
        }
        return NoSelection;
    }

    UInt32 RenderGraphPanel::CountSharedTextures() const noexcept
    {
        UInt32 shared = 0;
        for (UInt32 share = 0; share < m_physicalCount; ++share)
        {
            UInt32 users = 0;
            for (const TextureRow& texture : m_textures)
            {
                if (texture.share == share)
                {
                    ++users;
                }
            }
            if (users > 1)
            {
                ++shared;
            }
        }
        return shared;
    }
}
