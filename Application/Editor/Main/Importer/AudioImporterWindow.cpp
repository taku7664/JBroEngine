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

	const char* busItems[] = {
		Loc::Text(EditorLocKeys::InspectorAudioBusMaster),
		Loc::Text(EditorLocKeys::InspectorAudioBusMusic),
		Loc::Text(EditorLocKeys::InspectorAudioBusSfx),
		Loc::Text(EditorLocKeys::InspectorAudioBusVoice),
		Loc::Text(EditorLocKeys::InspectorAudioBusUi),
		Loc::Text(EditorLocKeys::InspectorAudioBusCustom),
	};
	int busIndex = static_cast<int>(m_options.DefaultBus);
	ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorAudioDefaultBus, EditorLocKeys::InspectorAudioDefaultBusDesc,
		[&]() {
			if (ImGui::Combo("##importer.audio.bus", &busIndex, busItems, IM_ARRAYSIZE(busItems)))
			{
				m_options.DefaultBus = static_cast<EAudioBusKind>(busIndex);
			}
		});

	ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorAudioDefaultVolume, EditorLocKeys::InspectorAudioDefaultVolumeDesc,
		[&]() { ImGui::DragFloat("##importer.audio.default_volume", &m_options.DefaultVolume, 0.01f, 0.0f, 2.0f); });
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
		errorOut = "Failed to write .Jmeta";
		return false;
	}
	return true;
}
