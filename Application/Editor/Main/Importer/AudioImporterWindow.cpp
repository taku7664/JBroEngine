#include "pch.h"
#include "AudioImporterWindow.h"

#include "Engine/Core/Asset/AssetMetaFile.h"
#include "Engine/Core/Asset/AssetPath.h"
#include "Engine/Editor/ImGuiUtillity.h"
#include "Engine/Editor/ImItem/ImText.h"

#include <filesystem>

namespace
{
	template <typename TDrawFunc>
	void RowWithTooltip(ImGui::Utillity::FormLayout& layout, const char* labelKey, const char* descKey, TDrawFunc&& drawFunc)
	{
		layout.Row(
			[&]() {
				ImText label;
				label.SetHoveredTooltip(Loc::Text(descKey));
				label(Loc::Text(labelKey));
			},
			std::forward<TDrawFunc>(drawFunc));
	}
}

void CAudioImporterWindow::DrawImportOptions()
{
	ImGui::Utillity::FormLayout layout("##audio_importer_options", 4.0f, {2.0f, 1.0f}, 140.0f);

	const char* modeItems[] = {
		Loc::Text("inspector.audio.mode.decompressed"),
		Loc::Text("inspector.audio.mode.streaming"),
	};
	int modeIndex = static_cast<int>(m_options.Mode);
	RowWithTooltip(layout, "inspector.audio.mode", "inspector.audio.mode.desc",
		[&]() {
			if (ImGui::Combo("##importer.audio.mode", &modeIndex, modeItems, IM_ARRAYSIZE(modeItems)))
			{
				m_options.Mode = static_cast<EAudioImportMode>(modeIndex);
			}
		});

	const char* busItems[] = {
		Loc::Text("inspector.audio.bus.master"),
		Loc::Text("inspector.audio.bus.music"),
		Loc::Text("inspector.audio.bus.sfx"),
		Loc::Text("inspector.audio.bus.voice"),
		Loc::Text("inspector.audio.bus.ui"),
		Loc::Text("inspector.audio.bus.custom"),
	};
	int busIndex = static_cast<int>(m_options.DefaultBus);
	RowWithTooltip(layout, "inspector.audio.default_bus", "inspector.audio.default_bus.desc",
		[&]() {
			if (ImGui::Combo("##importer.audio.bus", &busIndex, busItems, IM_ARRAYSIZE(busItems)))
			{
				m_options.DefaultBus = static_cast<EAudioBusKind>(busIndex);
			}
		});

	RowWithTooltip(layout, "inspector.audio.default_volume", "inspector.audio.default_volume.desc",
		[&]() { ImGui::DragFloat("##importer.audio.default_volume", &m_options.DefaultVolume, 0.01f, 0.0f, 2.0f); });
	RowWithTooltip(layout, "inspector.audio.loop", "inspector.audio.loop.desc",
		[&]() { ImGui::Checkbox("##importer.audio.loop", &m_options.Loop); });
	RowWithTooltip(layout, "inspector.audio.is_3d", "inspector.audio.is_3d.desc",
		[&]() { ImGui::Checkbox("##importer.audio.is_3d", &m_options.Is3D); });

	if (m_options.Is3D)
	{
		RowWithTooltip(layout, "inspector.audio.min_distance", "inspector.audio.min_distance.desc",
			[&]() { ImGui::DragFloat("##importer.audio.min_distance", &m_options.MinDistance, 0.1f, 0.0f, 10000.0f); });
		RowWithTooltip(layout, "inspector.audio.max_distance", "inspector.audio.max_distance.desc",
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
