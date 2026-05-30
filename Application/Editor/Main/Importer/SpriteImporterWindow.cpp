#include "pch.h"
#include "SpriteImporterWindow.h"

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

void CSpriteImporterWindow::DrawImportOptions()
{
	ImGui::Utillity::FormLayout layout("##sprite_importer_options", 4.0f, {2.0f, 1.0f}, 120.0f);

	// 슬라이스 모드
	const char* sliceItems[] = {
		Loc::Text("inspector.slice_type.none"),
		Loc::Text("inspector.slice_type.automatic"),
		Loc::Text("inspector.slice_type.cell_size"),
		Loc::Text("inspector.slice_type.cell_count"),
	};
	int sliceIndex = static_cast<int>(m_options.SliceType);
	RowWithTooltip(layout, "inspector.slice_type", "inspector.slice_type.desc",
		[&]() {
			if (ImGui::Combo("##importer.slice_type", &sliceIndex, sliceItems, IM_ARRAYSIZE(sliceItems)))
			{
				m_options.SliceType = static_cast<ESpriteSliceType>(sliceIndex);
			}
		});

	int rowCount    = static_cast<int>(m_options.RowCount);
	int columnCount = static_cast<int>(m_options.ColumnCount);
	int cellWidth   = static_cast<int>(m_options.CellWidth);
	int cellHeight  = static_cast<int>(m_options.CellHeight);
	int marginX     = static_cast<int>(m_options.MarginX);
	int marginY     = static_cast<int>(m_options.MarginY);
	int gapX        = static_cast<int>(m_options.GapX);
	int gapY        = static_cast<int>(m_options.GapY);

	if (ESpriteSliceType::CellCount == m_options.SliceType)
	{
		RowWithTooltip(layout, "inspector.row_count",    "inspector.row_count.desc",    [&]() { ImGui::InputInt("##importer.row_count",    &rowCount); });
		RowWithTooltip(layout, "inspector.column_count", "inspector.column_count.desc", [&]() { ImGui::InputInt("##importer.column_count", &columnCount); });
	}
	else if (ESpriteSliceType::CellSize == m_options.SliceType)
	{
		RowWithTooltip(layout, "inspector.cell_width",  "inspector.cell_width.desc",  [&]() { ImGui::InputInt("##importer.cell_width",  &cellWidth); });
		RowWithTooltip(layout, "inspector.cell_height", "inspector.cell_height.desc", [&]() { ImGui::InputInt("##importer.cell_height", &cellHeight); });
	}
	if (ESpriteSliceType::CellSize == m_options.SliceType || ESpriteSliceType::CellCount == m_options.SliceType)
	{
		RowWithTooltip(layout, "inspector.margin_x", "inspector.margin_x.desc", [&]() { ImGui::InputInt("##importer.margin_x", &marginX); });
		RowWithTooltip(layout, "inspector.margin_y", "inspector.margin_y.desc", [&]() { ImGui::InputInt("##importer.margin_y", &marginY); });
		RowWithTooltip(layout, "inspector.gap_x",    "inspector.gap_x.desc",    [&]() { ImGui::InputInt("##importer.gap_x",    &gapX); });
		RowWithTooltip(layout, "inspector.gap_y",    "inspector.gap_y.desc",    [&]() { ImGui::InputInt("##importer.gap_y",    &gapY); });
	}
	RowWithTooltip(layout, "inspector.pivot_x",         "inspector.pivot_x.desc",         [&]() { ImGui::DragFloat("##importer.pivot_x", &m_options.PivotX, 0.01f, 0.0f, 1.0f); });
	RowWithTooltip(layout, "inspector.pivot_y",         "inspector.pivot_y.desc",         [&]() { ImGui::DragFloat("##importer.pivot_y", &m_options.PivotY, 0.01f, 0.0f, 1.0f); });
	RowWithTooltip(layout, "inspector.pixels_per_unit", "inspector.pixels_per_unit.desc", [&]() { ImGui::DragFloat("##importer.ppu",     &m_options.PixelsPerUnit, 1.0f, 1.0f, 10000.0f); });

	m_options.RowCount    = static_cast<std::uint32_t>(std::max(1, rowCount));
	m_options.ColumnCount = static_cast<std::uint32_t>(std::max(1, columnCount));
	m_options.CellWidth   = static_cast<std::uint32_t>(std::max(1, cellWidth));
	m_options.CellHeight  = static_cast<std::uint32_t>(std::max(1, cellHeight));
	m_options.MarginX     = static_cast<std::uint32_t>(std::max(0, marginX));
	m_options.MarginY     = static_cast<std::uint32_t>(std::max(0, marginY));
	m_options.GapX        = static_cast<std::uint32_t>(std::max(0, gapX));
	m_options.GapY        = static_cast<std::uint32_t>(std::max(0, gapY));
}

bool CSpriteImporterWindow::ExecuteImport(const File::Path& sourcePath,
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

	// .Jmeta 동봉 — Sprite 옵션을 yaml 로.
	AssetMetaData meta;
	meta.Guid              = CAssetPath::GenerateAssetGuid();
	meta.Type              = EAssetType::Sprite;
	meta.Version           = 1;
	meta.Path              = File::Path(destFilePath);
	meta.MetaPath          = File::Path(CAssetPath::MakeMetaPath(destFilePath.generic_string().c_str()));
	meta.DisplayName       = CAssetPath::GetDisplayNameFromPath(destFilePath.generic_string().c_str());
	meta.Importer          = "Sprite";
	meta.ImportOptionsYaml = CSpriteImportOptions::ToYaml(m_options);

	if (false == CAssetMetaFile::Save(meta.MetaPath, meta))
	{
		errorOut = "Failed to write .Jmeta";
		return false;
	}
	return true;
}
