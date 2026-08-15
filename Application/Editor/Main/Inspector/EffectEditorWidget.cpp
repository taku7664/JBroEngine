#include "pch.h"
#include "EffectEditorWidget.h"

#include "Editor/Editor.h"
#include "Editor/EditorContext.h"
#include "Editor/Path/EditorPathUtils.h"
#include "Editor/Main/Inspector/EditorAudioPreview.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/IAssetRegistry.h"
#include "Engine/Core/Localization/LocalizationManager.h"
#include "ThirdParty/imgui/imgui.h"

#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace
{
	// Kind 별 표준 파라미터 키 + 권장 범위 — G-4 의 IAudioEffect::SetParameter 와 키 일치.
	struct EffectParamSpec { const char* Key; float Default; float Min; float Max; };

	std::vector<EffectParamSpec> EffectParamSpecs(EAudioEffectKind kind)
	{
		switch (kind)
		{
		case EAudioEffectKind::Reverb:
			return {
				{ "roomSize", 0.5f, 0.0f, 1.0f },
				{ "damping",  0.5f, 0.0f, 1.0f },
				{ "width",    1.0f, 0.0f, 1.0f },
				{ "wet",      0.3f, 0.0f, 1.0f },
				{ "dry",      0.7f, 0.0f, 1.0f },
			};
		case EAudioEffectKind::LowPass:
		case EAudioEffectKind::HighPass:
			return {
				{ "cutoff", 1000.0f, 20.0f, 20000.0f },
				{ "q",      0.707f,  0.1f,  10.0f },
			};
		case EAudioEffectKind::Echo:
			return {
				{ "delay", 0.25f, 0.0f, 2.0f },
				{ "decay", 0.5f,  0.0f, 1.0f },
				{ "wet",   0.4f,  0.0f, 1.0f },
			};
		case EAudioEffectKind::Distortion:
			return { { "drive", 0.5f, 0.0f, 1.0f }, { "wet", 1.0f, 0.0f, 1.0f } };
		case EAudioEffectKind::Compressor:
		case EAudioEffectKind::Limiter:
			return {
				{ "threshold", -12.0f, -60.0f, 0.0f },
				{ "ratio",      4.0f,   1.0f,  20.0f },
			};
		default:
			return {};
		}
	}

	// 현재 Kind 의 표준 키로 정규화 — 없는 키는 기본값 추가, 다른 키는 버림.
	void NormalizeEffectParams(AudioEffectData& data)
	{
		const std::vector<EffectParamSpec> specs = EffectParamSpecs(data.Kind);
		std::map<std::string, float> normalized;
		for (const EffectParamSpec& spec : specs)
		{
			auto it = data.Parameters.find(spec.Key);
			normalized[spec.Key] = (it != data.Parameters.end()) ? it->second : spec.Default;
		}
		data.Parameters = std::move(normalized);
	}
}

void CEffectEditorWidget::SetTargetGuid(const AssetGuid& guid)
{
	if (m_guid == guid && m_loaded) return;
	m_guid   = guid;
	m_loaded = false;
	LoadFromDisk();
}

void CEffectEditorWidget::LoadFromDisk()
{
	m_data  = AudioEffectData{};
	m_dirty = false;

	SafePtr<IAssetManager>   am = EditorContext::GetAssetManager();
	if (am.IsValid())
	{
		AssetMetaData meta;
		File::Path resolvedPath;
		if (am->GetRegistry().TryGetAsset(m_guid, meta) && am->ResolveAssetPath(meta.Path, resolvedPath))
		{
			std::ifstream stream(resolvedPath, std::ios::binary);
			if (stream.is_open())
			{
				std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
				m_data = CAudioEffectSerializer::FromYaml(text);
			}
		}
	}
	NormalizeEffectParams(m_data);
	m_loaded = true;
}

bool CEffectEditorWidget::SaveToDisk()
{
	SafePtr<IAssetManager>   am = EditorContext::GetAssetManager();
	if (false == am.IsValid()) return false;

	AssetMetaData meta;
	File::Path resolvedPath;
	if (false == am->GetRegistry().TryGetAsset(m_guid, meta) || false == am->ResolveAssetPath(meta.Path, resolvedPath)) return false;

	const std::string yaml = CAudioEffectSerializer::ToYaml(m_data);
	std::ofstream stream(resolvedPath, std::ios::binary | std::ios::trunc);
	if (false == stream.is_open()) return false;
	stream.write(yaml.data(), static_cast<std::streamsize>(yaml.size()));
	stream.close();

	// 로드되어 있으면 in-place 갱신 — 외부 AssetRef(캔버스/미리듣기) 보존.
	if (AssetRef<IAsset> loaded = am->FindLoadedAsset(m_guid))
	{
		loaded->ApplyImportOptions(yaml);
	}
	return true;
}

bool CEffectEditorWidget::Save()
{
	if (false == SaveToDisk())
	{
		return false;
	}
	m_dirty = false;
	return true;
}

void CEffectEditorWidget::Reload()
{
	// 편집 중이던 값을 버리고 디스크 내용으로 되돌린다.
	// SetTargetGuid 는 같은 guid 면 아무것도 안 하므로 여기서는 직접 읽는다.
	LoadFromDisk();
}

void CEffectEditorWidget::Draw()
{
	if (false == m_loaded) LoadFromDisk();

	bool changed = false;

	// ── Kind ───────────────────────────────────────────────────────────────
	const char* kindItems[] = {
		"Reverb", "LowPass", "HighPass", "Echo", "Distortion", "Compressor", "Limiter",
	};
	int kindIndex = static_cast<int>(m_data.Kind);
	if (kindIndex < 0 || kindIndex >= IM_ARRAYSIZE(kindItems)) kindIndex = 0;
	ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorEffectKind));
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::Combo("##effect.kind", &kindIndex, kindItems, IM_ARRAYSIZE(kindItems)))
	{
		m_data.Kind = static_cast<EAudioEffectKind>(kindIndex);
		NormalizeEffectParams(m_data);   // Kind 바뀌면 파라미터 키 교체.
		changed = true;
	}

	ImGui::Separator();

	// ── 파라미터 슬라이더 (Kind 표준 키) ─────────────────────────────────────
	const std::vector<EffectParamSpec> specs = EffectParamSpecs(m_data.Kind);
	for (const EffectParamSpec& spec : specs)
	{
		float value = m_data.Parameters.count(spec.Key) ? m_data.Parameters[spec.Key] : spec.Default;
		ImGui::TextUnformatted(spec.Key);
		ImGui::SetNextItemWidth(-FLT_MIN);
		const std::string label = std::string("##effect.") + spec.Key;
		if (ImGui::SliderFloat(label.c_str(), &value, spec.Min, spec.Max))
		{
			m_data.Parameters[spec.Key] = value;
			changed = true;
		}
	}

	if (changed)
	{
		m_dirty = true;

		// 미리듣기가 이 테스트 사운드를 재생 중이면 효과 옵션을 즉시 반영한다(재생 끊김 없음).
		if (EditorAudioPreview::IsPlaying()
			&& false == m_testSoundGuid.IsNull()
			&& EditorAudioPreview::GetCurrentGuid() == m_testSoundGuid)
		{
			EditorAudioPreview::UpdatePreviewEffect(m_data.Kind, m_data.Parameters);
		}
	}

	ImGui::Separator();
	if (ImActionButton(Loc::Text(EditorLocKeys::InspectorEffectApply))
		.Severity(EImValidationSeverity::Success)
		.Disabled(false == m_dirty)
		.Draw())
	{
		Save();
	}

	DrawPreview();
}

void CEffectEditorWidget::DrawPreview()
{
	ImSectionHeader(Loc::Text(EditorLocKeys::InspectorEffectPreview)).Draw();

	// 테스트 사운드 슬롯 — 오디오 자산을 드래그&드롭.
	ImAssetField("##effect_preview_sound", m_testSoundGuid)
		.Type(EAssetType::Audio)
		.Draw();
	ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::InspectorEffectPreviewHint));

	// 재생 / 정지 — 현재 편집 중인 효과를 테스트 사운드에 적용해 재생.
	const bool hasSound = false == m_testSoundGuid.IsNull();
	ImGui::BeginDisabled(false == hasSound);
	if (ImGui::Button(Loc::Text(EditorLocKeys::InspectorAudioPreviewPlay), ImVec2(80.0f, 0.0f)))
	{
		SafePtr<IAssetManager>   am = EditorContext::GetAssetManager();
		if (am.IsValid())
		{
			AssetMetaData meta;
			File::Path absPath;
			if (am->GetRegistry().TryGetAsset(m_testSoundGuid, meta)) am->ResolveAssetPath(meta.Path, absPath);
			if (false == absPath.empty())
			{
				const std::string utf8 = EditorPathUtils::ToUtf8(absPath);
				EditorAudioPreview::PlayFileWithEffect(utf8.c_str(), m_testSoundGuid, m_data.Kind, m_data.Parameters);
			}
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button(Loc::Text(EditorLocKeys::InspectorAudioPreviewStop), ImVec2(80.0f, 0.0f)))
	{
		EditorAudioPreview::Stop();
	}
}
