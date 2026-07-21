#pragma once

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  ScriptSchemaWidgets ─ 스크립트 프로퍼티 편집 ImGui 위젯 (헤더 전용 inline).
//  스크립트 생성 팝업(AssetBrowser)과 인스펙터 스키마 에디터가 공유.
//
//  한 행: [타입(+Ref) 콤보][이름][⋮ 메뉴] (핸들 "=" / 제거 "X" 는 ImList 담당)
//    ⋮ 메뉴: 표시 이름 / 카테고리 / 툴팁 / 직렬화 여부 / 범위.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#include "ThirdParty/imgui/imgui.h"
#include "Editor/ImItem/ImText.h"
#include "Editor/ImItem/ImButton.h"
#include "Editor/ImItem/ImActionButton.h"
#include "Editor/ImItem/ImFilterCombo.h"
#include "Editor/ImItem/ImValidationMessage.h"
#include "Editor/ImGuiUtillity.h"
#include "Core/Localization/LocalizationManager.h"
#include "Editor/Script/ScriptSchema.h"
#include "Editor/Icons/FontAwesomeIcons.h"
#include "Editor/Localization/EditorLocalizationKeys.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ScriptSchemaUI
{
	// 라벨 + 설명 툴팁(ImText).
	inline void LabelWithTip(const char* locLabelKey, const char* locTipKey)
	{
		ImText label;
		label.SetHoveredTooltip(Loc::Text(locTipKey));
		label(Loc::Text(locLabelKey));
	}

	// 검색 필터가 달린 콤보. 선택이 바뀌면 true·*current 갱신.
	// (ImGui 콤보 팝업은 동시에 하나만 열리므로 필터 버퍼는 정적 1개 공유.)
	inline bool FilterableCombo(const char* id, int* current, const std::vector<std::string>& items)
	{
		return ImFilterCombo(id)
			.Items(items)
			.CurrentIndex(current)
			.FilterHint(Loc::Text(EditorLocKeys::CommonFilter))
			.Draw();
	}

	// 1차(타입) 콤보 + (Ref<Component>/Ref<Asset> 면) 2차(대상) 콤보. 변경 시 true.
	// 각 콤보 폭 colW. 콤보 사이 SameLine 처리(이름 인풋은 호출자가 이어 붙임).
	inline bool TypeAndRefCombos(ScriptSchema::Property& p, float colW)
	{
		bool changed = false;

		const std::vector<std::string>& baseLabels = ScriptSchema::BaseTypes();
		int typeCur = 0;
		for (int i = 0; i < static_cast<int>(baseLabels.size()); ++i)
		{
			if (p.TypeToken == baseLabels[i])
			{
				typeCur = i; break;
			}
		}
		ImGui::SetNextItemWidth(colW);
		if (FilterableCombo("##type", &typeCur, baseLabels))
		{
			p.TypeToken = baseLabels[typeCur];
			// Array 도 2차 콤보를 쓰므로 같이 초기화해야 한다 — 안 그러면 이전 타입의
			// 대상(예: CSpriteAsset)이 원소 타입 자리에 남아 Array<CSpriteAsset> 가 된다.
			if (ScriptSchema::IsRefToken(p.TypeToken) || ScriptSchema::IsArrayToken(p.TypeToken)
				|| ScriptSchema::IsTableToken(p.TypeToken))
			{
				ScriptSchema::ResetRefTargetForToken(p);
			}
			changed = true;
		}
		ImGui::Utillity::HoveredToolTip(Loc::Text(EditorLocKeys::ScriptPropTypeTooltip));

		if (ScriptSchema::NeedsTargetCombo(p.TypeToken))
		{
			const std::vector<ScriptSchema::RefTargetInfo> targets =
				ScriptSchema::IsArrayToken(p.TypeToken) ? ScriptSchema::ArrayElementTargets() :
				ScriptSchema::IsTableToken(p.TypeToken) ? ScriptSchema::TableKeyTargets() :
				(p.TypeToken == "Ref<Asset>")           ? ScriptSchema::AssetTargets()
				                                        : ScriptSchema::ComponentTargets();
			std::vector<std::string> tLabels;
			tLabels.reserve(targets.size());
			int tCur = 0;
			for (int i = 0; i < static_cast<int>(targets.size()); ++i)
			{
				tLabels.push_back(targets[i].Label);
				if (targets[i].TypeName == p.RefTarget)
				{
					tCur = i;
				}
			}
			ImGui::SameLine(0.0f, 4.0f);
			ImGui::SetNextItemWidth(colW);
			if (FilterableCombo("##reftarget", &tCur, tLabels))
			{
				p.RefTarget  = targets[tCur].TypeName;
				p.RefInclude = targets[tCur].Include;
				changed = true;
			}
			ImGui::Utillity::HoveredToolTip(Loc::Text(EditorLocKeys::ScriptPropReftargetTooltip));
		}

		// Table 만 3차 콤보(값 타입)가 붙는다. 2차는 키를 맡는다.
		if (ScriptSchema::NeedsValueCombo(p.TypeToken))
		{
			const std::vector<ScriptSchema::RefTargetInfo> valueTargets = ScriptSchema::ArrayElementTargets();
			std::vector<std::string> vLabels;
			vLabels.reserve(valueTargets.size());
			int vCur = 0;
			for (int i = 0; i < static_cast<int>(valueTargets.size()); ++i)
			{
				vLabels.push_back(valueTargets[i].Label);
				if (valueTargets[i].TypeName == p.ValueTarget)
				{
					vCur = i;
				}
			}
			ImGui::SameLine(0.0f, 4.0f);
			ImGui::SetNextItemWidth(colW);
			if (FilterableCombo("##valuetarget", &vCur, vLabels))
			{
				p.ValueTarget = valueTargets[vCur].TypeName;
				changed = true;
			}
			ImGui::Utillity::HoveredToolTip(Loc::Text(EditorLocKeys::ScriptPropValuetargetTooltip));
		}
		return changed;
	}

	// ⋮ 메뉴 팝업 내용: 카테고리 / 툴팁 / 직렬화 / 범위.
	inline bool DrawPropertyMenu(ScriptSchema::Property& p, bool /*nameReadOnly*/)
	{
		bool changed = false;
		// 표시 이름 = Name("..") 어트리뷰트(인스펙터 표시용, C++ 멤버명과 별개. 항상 편집 가능).
		ImGui::Utillity::FormLayout layout("##script_fields");
		layout.Row([]() { LabelWithTip(EditorLocKeys::ScriptPropDisplayName, EditorLocKeys::ScriptPropDisplayNameTooltip); }, [&]() {
				ImInputText in("##attributes");
				in.SetText(p.DisplayName);
				in.SetHintText(Loc::Text(EditorLocKeys::ScriptPropDisplayNameHint));
				if (in(ImGuiInputTextFlags_None))
				{
					p.DisplayName = static_cast<const char*>(in);
					changed = true;
				}
			});
		layout.Row([]() { LabelWithTip(EditorLocKeys::ScriptPropCategory, EditorLocKeys::ScriptPropCategoryTooltip); }, [&]() {
				ImInputText in("##category");
				in.SetText(p.Category);
				in.SetHintText(Loc::Text(EditorLocKeys::ScriptPropCategoryHint));
				if (in(ImGuiInputTextFlags_None))
				{
					p.Category = static_cast<const char*>(in);
					changed = true;
				}
			});
		layout.Row([]() { LabelWithTip(EditorLocKeys::ScriptPropTooltip, EditorLocKeys::ScriptPropTooltipTooltip); }, [&]() {
				ImInputText in("##tooltip");
				in.SetText(p.Tooltip);
				in.SetHintText(Loc::Text(EditorLocKeys::ScriptPropTooltipHint));
				if (in(ImGuiInputTextFlags_None))
				{
					p.Tooltip = static_cast<const char*>(in);
					changed = true;
				}
			});
		layout.Row([]() { LabelWithTip(EditorLocKeys::ScriptPropSerialize, EditorLocKeys::ScriptPropSerializeTooltip); }, [&]() {
			bool serialize = false == p.NoSerialize;
			if (ImGui::Checkbox("##serialize", &serialize))
			{
				p.NoSerialize = false == serialize;
				changed = true;
			}
		});
		if (ScriptSchema::SupportsRange(p.TypeToken))
		{
			layout.Row([]() { LabelWithTip(EditorLocKeys::ScriptPropRange, EditorLocKeys::ScriptPropRangeTooltip); }, [&]() {
				if (ImGui::Checkbox("##range", &p.HasRange))
				{
					changed = true;
				}
			});
			if (p.HasRange)
			{
				layout.Row([]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::ScriptPropRangeMin)); }, [&]() {
					if (ImGui::InputDouble("##range_min", &p.RangeMin))
					{
						changed = true;
					}
				});
				layout.Row([]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::ScriptPropRangeMax)); }, [&]() {
					if (ImGui::InputDouble("##range_max", &p.RangeMax))
					{
						changed = true;
					}
				});
				if (p.RangeMin > p.RangeMax)
				{
					std::swap(p.RangeMin, p.RangeMax);
					changed = true;
				}
			}
		}
		else if (p.HasRange)
		{
			p.HasRange = false;
			changed = true;
		}
		return changed;
	}

	// ImList 한 행: [타입(+Ref) 콤보][이름 인풋][⋮ 메뉴]. 핸들 "=" / 제거 "X" 는 ImList 담당.
	// nameInvalid 면 이름 빨강, nameReadOnly 면 이름 편집 불가(인스펙터). 변경 시 true.
	inline bool DrawPropertyRow(ScriptSchema::Property& p, bool nameInvalid, bool nameReadOnly)
	{
		bool changed = false;

		const float fullW      = ImGui::CalcItemWidth();
		const float gap        = 4.0f;
		const bool  needsTarget= ScriptSchema::NeedsTargetCombo(p.TypeToken);
		const bool  needsValue = ScriptSchema::NeedsValueCombo(p.TypeToken);
		// 타입 (+Ref/원소/키 대상) (+Table 값). Table 만 3개다.
		const int   comboCount = 1 + (needsTarget ? 1 : 0) + (needsValue ? 1 : 0);
		const int   slots      = comboCount + 1;                   // 콤보들 + 이름
		const float menuW      = ImGui::GetFrameHeight();          // ⋮ 메뉴 폭(버튼 텍스트 대신 아이콘)
		const float avail      = fullW - menuW - gap * static_cast<float>(slots);
		const float colW       = (avail > 0.0f) ? (avail / static_cast<float>(slots)) : 60.0f;
		// 행 시작 X. 아래에서 ⋮ 를 오른쪽 끝에 **직접** 앉히는 데 쓴다.
		// colW 는 대개 소수라 위젯마다 픽셀로 반올림되고, 그 오차가 열 개수만큼 쌓인다.
		// 그래서 콤보가 3개인 Table 행이 1개인 Float 행보다 끝이 더 밀려 열이 어긋나 보였다.
		const float rowStartX  = ImGui::GetCursorPosX();

		changed |= TypeAndRefCombos(p, colW);

		// 이름 — 남은 공간을 그대로 먹는다. 누적된 반올림 오차를 여기서 흡수해야
		// 뒤따르는 ⋮ 가 모든 행에서 같은 X 에 온다.
		ImGui::SameLine(0.0f, gap);
		const float nameEndX = rowStartX + fullW - menuW - gap;
		const float nameW    = nameEndX - ImGui::GetCursorPosX();
		ImGui::SetNextItemWidth(nameW > 16.0f ? nameW : colW);
		{
			ImGui::BeginDisabled(nameReadOnly);
			ImInputText input;
			input.SetText(p.Name);
			input.SetHintText(Loc::Text(EditorLocKeys::ScriptPropNameHint));
			if (input(ImGuiInputTextFlags_None, nameInvalid)) { p.Name = static_cast<const char*>(input); changed = true; }
			ImGui::EndDisabled();
		}
		ImGui::Utillity::HoveredToolTip(Loc::Text(EditorLocKeys::ScriptPropNameTooltip));

		// 메뉴 — 행 오른쪽 끝에 고정한다(SameLine 이 누적 오차를 그대로 물려받지 않도록).
		ImGui::SameLine(0.0f, gap);
		ImGui::SetCursorPosX(rowStartX + fullW - menuW);
		if (ImTextButton(EditorIcons::ICON_ELLIPSIS_VERTICAL, ImVec2(menuW, 0.0f), ImVec2(0, -2)))
		{
			ImGui::OpenPopup("##prop_menu");   // U+22EE
		}
		ImGui::Utillity::HoveredToolTip(Loc::Text(EditorLocKeys::ScriptPropMenuTooltip));
		if (ImGui::BeginPopup("##prop_menu"))
		{
			if (DrawPropertyMenu(p, nameReadOnly)) changed = true;
			ImGui::EndPopup();
		}
		return changed;
	}
}
