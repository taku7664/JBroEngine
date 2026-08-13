#include "pch.h"
#include "SpriteImportOptionsEditor.h"

#include "Editor/EditorContext.h"
#include "Editor/ImItem/ImActionButton.h"
#include "Editor/ImItem/ImDragScalar.h"
#include "Editor/ImItem/ImItemTypes.h"
#include "Editor/Localization/EditorLocalizationKeys.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/Core/Asset/AssetMetaFile.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/TransientAssetLoad.h"
#include "Engine/Core/Localization/LocalizationManager.h"
#include "Engine/Editor/ImGuiUtillity.h"
#include "ThirdParty/imgui/imgui.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace
{
	struct EditState
	{
		SpriteImportOptions Options;
		bool                Dirty = false;
	};

	// guid 별 편집 중 상태. 에디터 세션 동안만 산다(작은 POD 라 정리 대상이 아니다).
	std::unordered_map<AssetGuid, EditState> g_states;

	EditState& AcquireState(const AssetMetaData& metaData)
	{
		auto found = g_states.find(metaData.Guid);
		if (found != g_states.end())
		{
			return found->second;
		}
		// 처음 보는 자산 → 디스크 값에서 시작한다. 매 프레임 다시 읽으면 사용자가 만진 값이
		// 1프레임 만에 되돌아간다.
		EditState state;
		state.Options = CSpriteImportOptions::FromYaml(metaData.ImportOptionsYaml);
		return g_states.emplace(metaData.Guid, std::move(state)).first->second;
	}

	bool TryGetTextureSize(const AssetGuid& guid, std::uint32_t& outWidth, std::uint32_t& outHeight)
	{
		outWidth = 0;
		outHeight = 0;
		SafePtr<IAssetManager> assetManager = EditorContext::GetAssetManager();
		if (false == assetManager.IsValid())
		{
			return false;
		}
		// 크기만 읽고 마는 일회성 로드 — 그냥 LoadAsset 하면 눌러 본 스프라이트가 전부
		// 캐시에 남는다(자동 GC 없음). 홀더가 대상이 바뀔 때 이전 것을 내린다.
		static CTransientAssetLoad s_sizeProbe;
		const AssetRef<IAsset>& loaded = s_sizeProbe.Acquire(*assetManager, guid);
		if (false == loaded.IsValid() || EAssetType::Sprite != loaded->GetAssetType())
		{
			return false;
		}
		const CSpriteAsset* spriteAsset = static_cast<const CSpriteAsset*>(loaded.Get());
		outWidth = spriteAsset->GetWidth();
		outHeight = spriteAsset->GetHeight();
		return true;
	}

	void DrawReadOnlyUInt(ImGui::Utillity::FormLayout& layout, const char* label, std::uint32_t value)
	{
		layout.Row(
			[&]() { ImGui::TextUnformatted(label); },
			[&]() { ImGui::TextDisabled("%u", value); });
	}

	bool SaveToDisk(const AssetMetaData& metaData, const SpriteImportOptions& options)
	{
		SafePtr<IAssetManager> assetManager = EditorContext::GetAssetManager();
		if (false == assetManager.IsValid())
		{
			return false;
		}

		File::Path resolvedMetaPath;
		if (false == assetManager->ResolveAssetPath(metaData.MetaPath, resolvedMetaPath))
		{
			return false;
		}

		AssetMetaData updatedMetaData = metaData;
		updatedMetaData.ImportOptionsYaml = CSpriteImportOptions::ToYaml(options);
		if (false == CAssetMetaFile::Save(resolvedMetaPath, updatedMetaData))
		{
			return false;
		}

		// 자산이 이미 로드되어 있으면 자산이 자기 ImportOptions 를 in-place 갱신한다.
		// 자산 객체는 destroy 되지 않으므로 외부 SafePtr(캔버스/미리보기 등)가 살아남는다.
		if (AssetRef<IAsset> loaded = assetManager->FindLoadedAsset(updatedMetaData.Guid))
		{
			loaded->ApplyImportOptions(updatedMetaData.ImportOptionsYaml);
		}
		return true;
	}
}

const SpriteImportOptions& SpriteImportOptionsEditor::Get(const AssetMetaData& metaData)
{
	return AcquireState(metaData).Options;
}

bool SpriteImportOptionsEditor::IsDirty(const AssetGuid& guid)
{
	const auto found = g_states.find(guid);
	return found != g_states.end() && found->second.Dirty;
}

void SpriteImportOptionsEditor::DrawEditor(const AssetMetaData& metaData)
{
	EditState& state = AcquireState(metaData);
	SpriteImportOptions& options = state.Options;

	int rowCount    = static_cast<int>(options.RowCount);
	int columnCount = static_cast<int>(options.ColumnCount);
	int cellWidth   = static_cast<int>(options.CellWidth);
	int cellHeight  = static_cast<int>(options.CellHeight);
	int marginX     = static_cast<int>(options.MarginX);
	int marginY     = static_cast<int>(options.MarginY);
	int gapX        = static_cast<int>(options.GapX);
	int gapY        = static_cast<int>(options.GapY);

	ImGui::Utillity::FormLayout layout("##sprite_import_options");
	bool changed = false;

	// ── 슬라이스 모드 ────────────────────────────────────────────────────────
	const char* sliceItems[] = {
		Loc::Text(EditorLocKeys::InspectorSliceTypeNone),
		Loc::Text(EditorLocKeys::InspectorSliceTypeAutomatic),
		Loc::Text(EditorLocKeys::InspectorSliceTypeCellSize),
		Loc::Text(EditorLocKeys::InspectorSliceTypeCellCount),
	};
	int sliceTypeIndex = static_cast<int>(options.SliceType);
	layout.Row(
		[&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorSliceType)); },
		[&]()
		{
			if (ImGui::Combo("##sprite_options.slice_type", &sliceTypeIndex, sliceItems, IM_ARRAYSIZE(sliceItems)))
			{
				changed = true;
			}
		});
	options.SliceType = static_cast<ESpriteSliceType>(sliceTypeIndex);

	// ── 모드별 입력란 ────────────────────────────────────────────────────────
	if (ESpriteSliceType::CellCount == options.SliceType)
	{
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorRowCount)); },    [&]() { changed |= ImDragInt("sprite_options.row_count").Range(1, MAX_CELL_COUNT).Speed(COUNT_DRAG_SPEED).Draw(rowCount); });
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorColumnCount)); }, [&]() { changed |= ImDragInt("sprite_options.column_count").Range(1, MAX_CELL_COUNT).Speed(COUNT_DRAG_SPEED).Draw(columnCount); });
	}
	else if (ESpriteSliceType::CellSize == options.SliceType)
	{
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorCellWidth)); },  [&]() { changed |= ImDragInt("sprite_options.cell_width").Range(1, MAX_PIXELS).Draw(cellWidth); });
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorCellHeight)); }, [&]() { changed |= ImDragInt("sprite_options.cell_height").Range(1, MAX_PIXELS).Draw(cellHeight); });
	}

	// ── 그리드 여백 (슬라이스 모드일 때만 의미가 있다) ───────────────────────
	if (ESpriteSliceType::CellSize == options.SliceType || ESpriteSliceType::CellCount == options.SliceType)
	{
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorMarginX)); }, [&]() { changed |= ImDragInt("sprite_options.margin_x").Range(0, MAX_PIXELS).Draw(marginX); });
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorMarginY)); }, [&]() { changed |= ImDragInt("sprite_options.margin_y").Range(0, MAX_PIXELS).Draw(marginY); });
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorGapX)); },    [&]() { changed |= ImDragInt("sprite_options.gap_x").Range(0, MAX_PIXELS).Draw(gapX); });
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorGapY)); },    [&]() { changed |= ImDragInt("sprite_options.gap_y").Range(0, MAX_PIXELS).Draw(gapY); });
	}

	// ── 피벗 / PPU ───────────────────────────────────────────────────────────
	SafePtr<CProjectManager> projectManager = EditorContext::GetProjectManager();
	const float projectPPU = projectManager ? projectManager->GetPixelsPerUnit() : 0.0f;

	layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorPivotX)); }, [&]() { changed |= ImGui::SliderFloat("##sprite_options.pivot_x", &options.PivotX, 0.0f, 1.0f, "%.2f"); });
	layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorPivotY)); }, [&]() { changed |= ImGui::SliderFloat("##sprite_options.pivot_y", &options.PivotY, 0.0f, 1.0f, "%.2f"); });
	layout.Row(
		[&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorPixelsPerUnit)); },
		[&]()
		{
			// 0 = 프로젝트 기본값 사용. 0 보다 크면 그 값으로 오버라이드.
			changed |= ImDragFloat("sprite_options.pixels_per_unit")
				.Range(0.0f, MAX_PIXELS_PER_UNIT)
				.Speed(1.0f)
				.Step(1.0f)
				.Format("%.1f")
				.Draw(options.PixelsPerUnit);
			if (options.PixelsPerUnit <= 0.0f)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%.1f %s", projectPPU, Loc::Text(EditorLocKeys::InspectorPpuProjectDefaultSuffix));
			}
		});

	options.RowCount    = static_cast<std::uint32_t>(std::max(1, rowCount));
	options.ColumnCount = static_cast<std::uint32_t>(std::max(1, columnCount));
	options.CellWidth   = static_cast<std::uint32_t>(std::max(1, cellWidth));
	options.CellHeight  = static_cast<std::uint32_t>(std::max(1, cellHeight));
	options.MarginX     = static_cast<std::uint32_t>(std::max(0, marginX));
	options.MarginY     = static_cast<std::uint32_t>(std::max(0, marginY));
	options.GapX        = static_cast<std::uint32_t>(std::max(0, gapX));
	options.GapY        = static_cast<std::uint32_t>(std::max(0, gapY));

	if (changed)
	{
		state.Dirty = true;
	}
}

void SpriteImportOptionsEditor::DrawSliceSummary(const AssetMetaData& metaData)
{
	const SpriteImportOptions& options = AcquireState(metaData).Options;

	std::uint32_t textureWidth = 0;
	std::uint32_t textureHeight = 0;
	TryGetTextureSize(metaData.Guid, textureWidth, textureHeight);

	const std::vector<SpriteFrame> frames = CSpriteImportOptions::BuildFrames(textureWidth, textureHeight, options);

	ImGui::Utillity::FormLayout layout("##sprite_slice_summary");
	DrawReadOnlyUInt(layout, Loc::Text(EditorLocKeys::InspectorSpritePreviewFrameCount),
		static_cast<std::uint32_t>(frames.size()));
	if (false == frames.empty())
	{
		DrawReadOnlyUInt(layout, Loc::Text(EditorLocKeys::InspectorSpriteFrameWidth), frames.front().Width);
		DrawReadOnlyUInt(layout, Loc::Text(EditorLocKeys::InspectorSpriteFrameHeight), frames.front().Height);
	}
}

void SpriteImportOptionsEditor::DrawApplyButton(const AssetMetaData& metaData)
{
	EditState& state = AcquireState(metaData);

	ImGui::BeginDisabled(false == state.Dirty);
	if (ImActionButton(Loc::Text(EditorLocKeys::InspectorApplySpriteImportOptions))
		.Severity(EImValidationSeverity::Success)
		.Draw())
	{
		if (SaveToDisk(metaData, state.Options))
		{
			state.Dirty = false;
		}
	}
	ImGui::EndDisabled();
}
