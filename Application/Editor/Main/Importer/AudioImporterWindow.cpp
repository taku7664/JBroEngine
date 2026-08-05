#include "pch.h"
#include "AudioImporterWindow.h"
#include "ImporterGui.h"

#include "Engine/Core/Asset/AssetMetaFile.h"
#include "Engine/Core/Asset/AssetPath.h"
#include "Engine/Editor/ImGuiUtillity.h"
#include "Editor/ImItem/ImText.h"

#include <filesystem>

void CAudioImporterWindow::DrawImportOptions()
{
	ImGui::Utillity::FormLayout layout("##audio_importer_options", 4.0f, {2.0f, 1.0f}, 140.0f);

	const char* modeItems[] = {
		Loc::Text(EditorLocKeys::InspectorAudioModeDecompressed),
		Loc::Text(EditorLocKeys::InspectorAudioModeStreaming),
	};
	int modeIndex = static_cast<int>(m_options.Mode);
	ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorAudioMode, EditorLocKeys::InspectorAudioModeDesc,
		[&]() {
			if (ImGui::Combo("##importer.audio.mode", &modeIndex, modeItems, IM_ARRAYSIZE(modeItems)))
			{
				m_options.Mode = static_cast<EAudioImportMode>(modeIndex);
			}
		});

	// 버스는 이름으로 지목한다 — 목록이 프로젝트마다 다르므로 고정 콤보를 둘 수 없다.
	// 프로젝트 세팅(오디오 버스)에 적은 이름을 그대로 쓴다. 빈 값은 Master 로 해석된다.
	ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorAudioDefaultBus, EditorLocKeys::InspectorAudioDefaultBusDesc,
		[&]() {
			ImInputText input("##importer.audio.bus");
			input.SetText(m_options.DefaultBus);
			input.SetHintText(AUDIO_MASTER_BUS_NAME);
			if (input(ImGuiInputTextFlags_None))
			{
				m_options.DefaultBus = static_cast<const char*>(input);
			}
		});

	ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorAudioDefaultVolume, EditorLocKeys::InspectorAudioDefaultVolumeDesc,
		[&]() { ImGui::SliderFloat("##importer.audio.default_volume", &m_options.DefaultVolume, 0.0f, 1.0f, "%.2f"); });
	ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorAudioLoop, EditorLocKeys::InspectorAudioLoopDesc,
		[&]() { ImGui::Checkbox("##importer.audio.loop", &m_options.Loop); });
	ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorAudioIs3d, EditorLocKeys::InspectorAudioIs3dDesc,
		[&]() { ImGui::Checkbox("##importer.audio.is_3d", &m_options.Is3D); });

	if (m_options.Is3D)
	{
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorAudioMinDistance, EditorLocKeys::InspectorAudioMinDistanceDesc,
			[&]() { ImGui::DragFloat("##importer.audio.min_distance", &m_options.MinDistance, 0.1f, 0.0f, 10000.0f); });
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorAudioMaxDistance, EditorLocKeys::InspectorAudioMaxDistanceDesc,
			[&]() { ImGui::DragFloat("##importer.audio.max_distance", &m_options.MaxDistance, 0.1f, 0.0f, 10000.0f); });
		if (m_options.MinDistance < 0.0f) m_options.MinDistance = 0.0f;
		if (m_options.MaxDistance < m_options.MinDistance) m_options.MaxDistance = m_options.MinDistance;
	}
}

bool CAudioImporterWindow::ExecuteImport(const File::Path& sourcePath,
                                         const File::Path& destFilePath,
                                         std::string& errorOut)
{
	std::error_code ec;
	std::filesystem::copy_file(sourcePath, destFilePath,
		std::filesystem::copy_options::overwrite_existing, ec);
	if (ec)
	{
		errorOut = ec.message();
		return false;
	}

	AssetMetaData meta;
	meta.Guid              = CAssetPath::GenerateAssetGuid();
	meta.Type              = EAssetType::Audio;
	meta.Version           = 1;
	meta.Path              = File::Path(destFilePath);
	meta.MetaPath          = File::Path(CAssetPath::MakeMetaPath(destFilePath.generic_string().c_str()));
	meta.DisplayName       = CAssetPath::GetDisplayNameFromPath(destFilePath.generic_string().c_str());
	meta.Importer          = "Audio";
	meta.ImportOptionsYaml = CAudioImportOptions::ToYaml(m_options);

	if (false == CAssetMetaFile::Save(meta.MetaPath, meta))
	{
		errorOut = "Failed to write .jmeta";
		return false;
	}
	return true;
}
