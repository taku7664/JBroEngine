#include "pch.h"
#include "SpriteImporterWindow.h"
#include "ImporterGui.h"

#include "Editor/EditorContext.h"

#include "Engine/Core/Asset/AssetMetaFile.h"
#include "Engine/Core/Asset/AssetPath.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/ImGuiUtillity.h"
#include "Engine/Editor/ImItem/ImText.h"
#include "Engine/Editor/Project/ProjectManager.h"

#include <filesystem>

void CSpriteImporterWindow::DrawImportOptions()
{
	ImGui::Utillity::FormLayout layout("##sprite_importer_options", 4.0f, {2.0f, 1.0f}, 120.0f);

	// 슬라이스 모드
	const char* sliceItems[] = {
		Loc::Text(EditorLocKeys::InspectorSliceTypeNone),
		Loc::Text(EditorLocKeys::InspectorSliceTypeAutomatic),
		Loc::Text(EditorLocKeys::InspectorSliceTypeCellSize),
		Loc::Text(EditorLocKeys::InspectorSliceTypeCellCount),
	};
	int sliceIndex = static_cast<int>(m_options.SliceType);
	ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorSliceType, EditorLocKeys::InspectorSliceTypeDesc,
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
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorRowCount,    EditorLocKeys::InspectorRowCountDesc,    [&]() { ImGui::InputInt("##importer.row_count",    &rowCount); });
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorColumnCount, EditorLocKeys::InspectorColumnCountDesc, [&]() { ImGui::InputInt("##importer.column_count", &columnCount); });
	}
	else if (ESpriteSliceType::CellSize == m_options.SliceType)
	{
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorCellWidth,  EditorLocKeys::InspectorCellWidthDesc,  [&]() { ImGui::InputInt("##importer.cell_width",  &cellWidth); });
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorCellHeight, EditorLocKeys::InspectorCellHeightDesc, [&]() { ImGui::InputInt("##importer.cell_height", &cellHeight); });
	}
	if (ESpriteSliceType::CellSize == m_options.SliceType || ESpriteSliceType::CellCount == m_options.SliceType)
	{
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorMarginX, EditorLocKeys::InspectorMarginXDesc, [&]() { ImGui::InputInt("##importer.margin_x", &marginX); });
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorMarginY, EditorLocKeys::InspectorMarginYDesc, [&]() { ImGui::InputInt("##importer.margin_y", &marginY); });
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorGapX,    EditorLocKeys::InspectorGapXDesc,    [&]() { ImGui::InputInt("##importer.gap_x",    &gapX); });
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorGapY,    EditorLocKeys::InspectorGapYDesc,    [&]() { ImGui::InputInt("##importer.gap_y",    &gapY); });
	}
	ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorPivotX,         EditorLocKeys::InspectorPivotXDesc,         [&]() { ImGui::DragFloat("##importer.pivot_x", &m_options.PivotX, 0.01f, 0.0f, 1.0f); });
	ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorPivotY,         EditorLocKeys::InspectorPivotYDesc,         [&]() { ImGui::DragFloat("##importer.pivot_y", &m_options.PivotY, 0.01f, 0.0f, 1.0f); });
	{
		SafePtr<CProjectManager> projectManager = EditorContext::GetProjectManager();
		const float projectPPU = projectManager ? projectManager->GetPixelsPerUnit() : 0.0f;
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorPixelsPerUnit, EditorLocKeys::InspectorPixelsPerUnitDesc,
			[&]() {
				// 0 = 프로젝트 기본값 사용. 0 보다 큰 값이면 그 값으로 오버라이드.
				ImGui::DragFloat("##importer.ppu", &m_options.PixelsPerUnit, 1.0f, 0.0f, 10000.0f);
				if (m_options.PixelsPerUnit <= 0.0f)
				{
					ImGui::SameLine();
					ImGui::TextDisabled("%.1f %s", projectPPU, Loc::Text(EditorLocKeys::InspectorPpuProjectDefaultSuffix));
				}
			});
	}

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
