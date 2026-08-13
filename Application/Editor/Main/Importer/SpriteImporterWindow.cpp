#include "pch.h"
#include "SpriteImporterWindow.h"
#include "ImporterGui.h"

#include "Editor/EditorContext.h"

#include "Engine/Core/Asset/AssetMetaFile.h"
#include "Engine/Core/Asset/AssetPath.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/ImGuiUtillity.h"
#include "Editor/ImItem/ImDragScalar.h"
#include "Editor/ImItem/ImText.h"
#include "Editor/Main/Importer/SpriteImportOptionsEditor.h"   // 슬라이스 수치 클램프 상수 공유
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
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorRowCount,    EditorLocKeys::InspectorRowCountDesc,    [&]() { ImDragInt("importer.row_count").Range(1, MAX_CELL_COUNT).Speed(COUNT_DRAG_SPEED).Draw(rowCount); });
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorColumnCount, EditorLocKeys::InspectorColumnCountDesc, [&]() { ImDragInt("importer.column_count").Range(1, MAX_CELL_COUNT).Speed(COUNT_DRAG_SPEED).Draw(columnCount); });
	}
	else if (ESpriteSliceType::CellSize == m_options.SliceType)
	{
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorCellWidth,  EditorLocKeys::InspectorCellWidthDesc,  [&]() { ImDragInt("importer.cell_width").Range(1, MAX_PIXELS).Draw(cellWidth); });
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorCellHeight, EditorLocKeys::InspectorCellHeightDesc, [&]() { ImDragInt("importer.cell_height").Range(1, MAX_PIXELS).Draw(cellHeight); });
	}
	if (ESpriteSliceType::CellSize == m_options.SliceType || ESpriteSliceType::CellCount == m_options.SliceType)
	{
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorMarginX, EditorLocKeys::InspectorMarginXDesc, [&]() { ImDragInt("importer.margin_x").Range(0, MAX_PIXELS).Draw(marginX); });
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorMarginY, EditorLocKeys::InspectorMarginYDesc, [&]() { ImDragInt("importer.margin_y").Range(0, MAX_PIXELS).Draw(marginY); });
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorGapX,    EditorLocKeys::InspectorGapXDesc,    [&]() { ImDragInt("importer.gap_x").Range(0, MAX_PIXELS).Draw(gapX); });
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorGapY,    EditorLocKeys::InspectorGapYDesc,    [&]() { ImDragInt("importer.gap_y").Range(0, MAX_PIXELS).Draw(gapY); });
	}
	ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorPivotX,         EditorLocKeys::InspectorPivotXDesc,         [&]() { ImGui::SliderFloat("##importer.pivot_x", &m_options.PivotX, 0.0f, 1.0f, "%.2f"); });
	ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorPivotY,         EditorLocKeys::InspectorPivotYDesc,         [&]() { ImGui::SliderFloat("##importer.pivot_y", &m_options.PivotY, 0.0f, 1.0f, "%.2f"); });
	{
		SafePtr<CProjectManager> projectManager = EditorContext::GetProjectManager();
		const float projectPPU = projectManager ? projectManager->GetPixelsPerUnit() : 0.0f;
		ImporterGui::DrawLocalizedRow(layout, EditorLocKeys::InspectorPixelsPerUnit, EditorLocKeys::InspectorPixelsPerUnitDesc,
			[&]() {
				// 0 = 프로젝트 기본값 사용. 0 보다 큰 값이면 그 값으로 오버라이드.
				ImDragFloat("importer.ppu")
					.Range(0.0f, MAX_PIXELS_PER_UNIT)
					.Speed(1.0f)
					.Step(1.0f)
					.Format("%.1f")
					.Draw(m_options.PixelsPerUnit);
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

	// .jmeta 동봉 — Sprite 옵션을 yaml 로.
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
		errorOut = "Failed to write .jmeta";
		return false;
	}
	return true;
}
