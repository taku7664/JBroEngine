#include "pch.h"
#include "AssetInspectorPreview.h"
#include "EditorAudioPreview.h"

#include "Editor/Editor.h"
#include "Engine/Core/Asset/AssetTypes.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/SpriteAsset.h"
#include "Engine/Core/Asset/AudioAsset.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Core/Localization/LocalizationManager.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/IRHITexture.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/ImGuiUtillity.h"
#include "Engine/Editor/ImItem/ImAudioVisualizer.h"
#include "Engine/Editor/ImItem/ImSpectrumVisualizer.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "ThirdParty/imgui/imgui.h"

#include <algorithm>
#include <memory>
#include <string>

namespace
{
    SafePtr<IAssetManager> GetAssetManager()
    {
        SafePtr<CProjectManager> pm = Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
        return pm ? pm->GetAssetManager() : nullptr;
    }

    // ── Sprite handler ───────────────────────────────────────────────────────
    // Sprite 는 별도 자원 관리가 필요 없어 OnEnter/OnExit 는 비워둔다.
    class CSpritePreviewHandler final : public IAssetInspectorPreviewHandler
    {
    public:
        bool CanPreview(const AssetMetaData& metaData) const override
        {
            return EAssetType::Sprite == metaData.Type;
        }

        bool OnStay(const AssetMetaData& metaData) override
        {
            SafePtr<IAssetManager> am = GetAssetManager();
            if (false == am.IsValid()) return false;

            SafePtr<IAsset> asset = am->LoadAsset(metaData.Guid);
            if (false == asset.IsValid() || EAssetType::Sprite != asset->GetAssetType())
            {
                return false;
            }

            CSpriteAsset* sprite = static_cast<CSpriteAsset*>(asset.TryGet());
            const std::uint32_t w = sprite->GetWidth();
            const std::uint32_t h = sprite->GetHeight();
            if (0 == w || 0 == h) return false;

            if (Engine.RHIDevice.IsValid())
            {
                sprite->EnsureGpuTexture(*Engine.RHIDevice);
            }
            SafePtr<IRHITexture> tex = sprite->GetGpuTexture();
            if (false == tex.IsValid()) return false;

            const void* srv = tex->GetNativeHandle().ShaderResourceView;
            if (nullptr == srv) return false;

            constexpr float MAX_H = 200.0f;
            const float avail = std::max(32.0f, ImGui::GetContentRegionAvail().x);
            const float aspect = static_cast<float>(w) / static_cast<float>(h);
            float drawW = std::min(avail, static_cast<float>(w));
            float drawH = drawW / aspect;
            if (drawH > MAX_H)
            {
                drawH = MAX_H;
                drawW = drawH * aspect;
            }

            const float cursorX = ImGui::GetCursorPosX() + (avail - drawW) * 0.5f;
            ImGui::SetCursorPosX(cursorX);
            ImGui::Image(reinterpret_cast<ImTextureID>(const_cast<void*>(srv)), ImVec2(drawW, drawH));

            ImGui::TextDisabled("%u x %u", w, h);
            return true;
        }
    };

    // ── Audio handler ────────────────────────────────────────────────────────
    //  OnEnter : 자산 PCM 으로 waveform / spectrum 셋업 (1 회)
    //  OnStay  : 매 프레임 그리기 + 인터랙션
    //  OnExit  : 재생 정지 + spectrum unbind + visualizer clear (자원 즉시 해제)
    class CAudioPreviewHandler final : public IAssetInspectorPreviewHandler
    {
    public:
        bool CanPreview(const AssetMetaData& metaData) const override
        {
            return EAssetType::Audio == metaData.Type;
        }

        void OnEnter(const AssetMetaData& metaData) override
        {
            // 이전 자산 잔여물 클린업 — 방어적.
            m_visualizer.Clear();
            m_spectrum.Unbind();
            m_isStreaming = false;

            EditorAudioPreview::EnsureInitialized();

            SafePtr<IAssetManager> am = GetAssetManager();
            if (false == am.IsValid()) return;

            SafePtr<IAsset> asset = am->LoadAsset(metaData.Guid);
            if (false == asset.IsValid() || EAssetType::Audio != asset->GetAssetType()) return;

            CAudioAsset* audio = static_cast<CAudioAsset*>(asset.TryGet());
            if (audio->IsStreaming())
            {
                m_isStreaming = true;
                return;
            }

            const auto& pcm = audio->GetPcmData();
            const auto& fmt = audio->GetFormat();
            if (pcm.empty() || 0 == fmt.Channels) return;

            // 정적 파형 시각화 — PCM 한 번 분석해 peak summary 캐시.
            const auto waveFmt = (EAudioFormat::PCM_S16 == fmt.Format)
                ? ImAudioVisualizer::ESampleFormat::S16
                : ImAudioVisualizer::ESampleFormat::F32;
            m_visualizer
                .SetBarCount(128)
                .SetBarThickness(2.0f)
                .SetBarGap(1.0f)
                .SetColor      (IM_COL32(110, 110, 110, 255))
                .SetPlayedColor(IM_COL32(255, 255, 255, 255))
                .SetAmplitudeGain(1.0f);
            m_visualizer.SetPcmData(pcm.data(), pcm.size(),
                fmt.SampleRate, fmt.Channels, waveFmt);

            // 실시간 스펙트럼 — PCM 포인터를 그대로 참조.  CAudioAsset 이 LoadAsset
            // 으로 hold 되어 있는 동안 유효, OnExit 에서 반드시 Unbind 한다.
            const auto specFmt = (EAudioFormat::PCM_S16 == fmt.Format)
                ? ImSpectrumVisualizer::ESampleFormat::S16
                : ImSpectrumVisualizer::ESampleFormat::F32;
            m_spectrum
                .SetBarCount(64)
                .SetBarGap(2.0f)
                .SetSmoothingFactor(0.65f)
                .SetPeakDecay(1.4f)
                .SetMinFreq(60.0f)
                .SetMaxFreq(16000.0f)
                .SetRounding(2.0f)
                .SetColorLow (IM_COL32( 80, 140, 240, 255))
                .SetColorMid (IM_COL32( 60, 220, 130, 255))
                .SetColorHigh(IM_COL32(240,  80, 180, 255));
            m_spectrum.BindPcm(pcm.data(), pcm.size(),
                fmt.SampleRate, fmt.Channels, specFmt);
        }

        bool OnStay(const AssetMetaData& metaData) override
        {
            const bool playing = EditorAudioPreview::IsPlaying();
            const bool isCurrent = (EditorAudioPreview::GetCurrentGuid() == metaData.Guid)
                && EditorAudioPreview::GetCurrentGuid().IsNull() == false;

            // ── 정적 파형 시각화 ──────────────────────────────────────
            const double totalForVis = EditorAudioPreview::GetCurrentDurationSeconds();
            const double curForVis   = EditorAudioPreview::GetCurrentPositionSeconds();
            const float  frac = (isCurrent && totalForVis > 0.0)
                ? static_cast<float>(curForVis / totalForVis) : 0.0f;
            if (m_visualizer.HasData())
            {
                m_visualizer.SetPlayheadFraction(frac);
                m_visualizer(ImVec2(-FLT_MIN, 56.0f));
            }
            else if (m_isStreaming)
            {
                ImGui::TextDisabled("%s", Loc::Text("inspector.audio.preview.streaming_no_waveform"));
            }

            // ── 실시간 스펙트럼 ───────────────────────────────────────
            if (m_spectrum.HasPcm())
            {
                const float dt = ImGui::GetIO().DeltaTime;
                const double posSec = isCurrent ? curForVis : 0.0;
                m_spectrum.Tick(posSec, dt);
                m_spectrum(ImVec2(-FLT_MIN, 70.0f));
            }

            // ── Play / Stop ──────────────────────────────────────────
            ImGui::BeginDisabled(playing);
            if (ImGui::Button(Loc::Text("inspector.audio.preview.play"), ImVec2(80.0f, 0.0f)))
            {
                SafePtr<CProjectManager> pm = Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
                if (pm)
                {
                    File::Path absPath;
                    if (SafePtr<IAssetManager> am = pm->GetAssetManager())
                    {
                        am->ResolveAssetPath(metaData.Path, absPath);
                    }
                    if (absPath.empty()) absPath = metaData.Path;
                    const auto u8 = absPath.generic_u8string();
                    const std::string utf8Path(reinterpret_cast<const char*>(u8.c_str()), u8.size());
                    EditorAudioPreview::PlayFile(utf8Path.c_str(), metaData.Guid);
                }
            }
            ImGui::Utillity::HoveredToolTip(Loc::Text("inspector.audio.preview.play.desc"));
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(false == playing && false == isCurrent);
            if (ImGui::Button(Loc::Text("inspector.audio.preview.stop"), ImVec2(80.0f, 0.0f)))
            {
                EditorAudioPreview::Stop();
            }
            ImGui::Utillity::HoveredToolTip(Loc::Text("inspector.audio.preview.stop.desc"));
            ImGui::EndDisabled();

            // 진행/스크럽
            const double total = EditorAudioPreview::GetCurrentDurationSeconds();
            const double cur   = EditorAudioPreview::GetCurrentPositionSeconds();
            if (isCurrent && total > 0.0)
            {
                ImGui::SameLine();
                float pos = static_cast<float>(std::min(static_cast<double>(total), std::max(0.0, cur)));
                char fmt[64];
                std::snprintf(fmt, sizeof(fmt), "%.2f / %.2f s", static_cast<double>(pos), total);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderFloat("##audio.preview.pos", &pos, 0.0f, static_cast<float>(total), fmt))
                {
                    EditorAudioPreview::SeekSeconds(static_cast<double>(pos));
                }
            }
            return true;
        }

        void OnExit() override
        {
            // 자원 즉시 해제 — 인스펙터 포커스가 떠나면 player / spectrum 모두 정리.
            EditorAudioPreview::Stop();
            m_spectrum.Unbind();
            m_visualizer.Clear();
            m_isStreaming = false;
        }

    private:
        ImAudioVisualizer    m_visualizer;
        ImSpectrumVisualizer m_spectrum;
        bool                 m_isStreaming = false;
    };

    // ── Registry ─────────────────────────────────────────────────────────────
    std::vector<std::unique_ptr<IAssetInspectorPreviewHandler>>& Handlers()
    {
        static std::vector<std::unique_ptr<IAssetInspectorPreviewHandler>> s_handlers;
        return s_handlers;
    }

    IAssetInspectorPreviewHandler*& ActiveHandler()
    {
        static IAssetInspectorPreviewHandler* s_active = nullptr;
        return s_active;
    }

    AssetGuid& ActiveGuid()
    {
        static AssetGuid s_guid = File::NULL_GUID;
        return s_guid;
    }

    bool& InitializedFlag()
    {
        static bool s_init = false;
        return s_init;
    }

    void EnsureRegistered()
    {
        if (InitializedFlag()) return;
        Handlers().emplace_back(std::make_unique<CSpritePreviewHandler>());
        Handlers().emplace_back(std::make_unique<CAudioPreviewHandler>());
        InitializedFlag() = true;
    }

    // 현재 활성 핸들러 → OnExit 호출 후 nullptr 로.
    void DeactivateCurrent()
    {
        if (IAssetInspectorPreviewHandler* h = ActiveHandler())
        {
            h->OnExit();
            ActiveHandler() = nullptr;
        }
        ActiveGuid() = File::NULL_GUID;
    }
}

namespace AssetInspectorPreview
{

bool DrawTopPreview(const AssetMetaData& metaData)
{
    EnsureRegistered();

    // 매칭 핸들러 탐색.
    IAssetInspectorPreviewHandler* matched = nullptr;
    for (auto& h : Handlers())
    {
        if (h->CanPreview(metaData))
        {
            matched = h.get();
            break;
        }
    }

    if (nullptr == matched)
    {
        // 표시할 게 없음 — 활성 핸들러가 있다면 마무리.
        DeactivateCurrent();
        return false;
    }

    // 핸들러가 바뀌었거나 같은 핸들러여도 자산이 바뀐 경우 Enter/Exit 갈아끼우기.
    if (matched != ActiveHandler() || ActiveGuid() != metaData.Guid)
    {
        DeactivateCurrent();
        matched->OnEnter(metaData);
        ActiveHandler() = matched;
        ActiveGuid()    = metaData.Guid;
    }

    return matched->OnStay(metaData);
}

void NotifyInspectionLost()
{
    EnsureRegistered();
    DeactivateCurrent();
}

void ShutdownAll()
{
    DeactivateCurrent();
    EditorAudioPreview::Shutdown();
    Handlers().clear();
    InitializedFlag() = false;
}

} // namespace AssetInspectorPreview
