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
#include "Editor/ImGuiUtillity.h"
#include "Core/Localization/LocalizationManager.h"
#include "Editor/Script/ScriptSchema.h"

#include <string>
#include <vector>

namespace ScriptSchemaUI
{
	// 직전 위젯에 짧은 지연 툴팁(로컬라이즈 키).
	inline void HoverTip(const char* locKey)
	{
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		{
			ImGui::SetTooltip("%s", Loc::Text(locKey));
		}
	}

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
		bool changed = false;
		const char* preview = (*current >= 0 && *current < static_cast<int>(items.size()))
			? items[*current].c_str() : "";
		// 드롭다운 스크롤바 숨김(휠 스크롤은 유지).
		ImGui::Utillity::StyleBuilder comboStyle;
		comboStyle.PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
		if (ImGui::BeginCombo(id, preview))
		{
			static char filter[64] = "";
			if (ImGui::IsWindowAppearing())
			{
				filter[0] = '\0';
				ImGui::SetKeyboardFocusHere();
			}
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##filter", Loc::Text("common.filter"), filter, sizeof(filter));
			ImGui::Separator();
			for (int i = 0; i < static_cast<int>(items.size()); ++i)
			{
				if (false == ScriptSchema::ContainsCaseInsensitive(items[i], filter)) continue;
				const bool selected = (i == *current);
				if (ImGui::Selectable(items[i].c_str(), selected))
				{
					*current = i;
					changed = true;
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		return changed;
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
			if (p.TypeToken == baseLabels[i]) { typeCur = i; break; }
		}
		ImGui::SetNextItemWidth(colW);
		if (FilterableCombo("##type", &typeCur, baseLabels))
		{
			p.TypeToken = baseLabels[typeCur];
			if (ScriptSchema::IsRefToken(p.TypeToken)) ScriptSchema::ResetRefTargetForToken(p);
			changed = true;
		}
		HoverTip("script_prop.type_tooltip");

		if (ScriptSchema::NeedsTargetCombo(p.TypeToken))
		{
			const std::vector<ScriptSchema::RefTargetInfo> targets =
				(p.TypeToken == "Ref<Asset>") ? ScriptSchema::AssetTargets() : ScriptSchema::ComponentTargets();
			std::vector<std::string> tLabels;
			tLabels.reserve(targets.size());
			int tCur = 0;
			for (int i = 0; i < static_cast<int>(targets.size()); ++i)
			{
				tLabels.push_back(targets[i].Label);
				if (targets[i].TypeName == p.RefTarget) tCur = i;
			}
			ImGui::SameLine(0.0f, 4.0f);
			ImGui::SetNextItemWidth(colW);
			if (FilterableCombo("##reftarget", &tCur, tLabels))
			{
				p.RefTarget  = targets[tCur].TypeName;
				p.RefInclude = targets[tCur].Include;
				changed = true;
			}
			HoverTip("script_prop.reftarget_tooltip");
		}
		return changed;
	}

	// ⋮ 메뉴 팝업 내용: 카테고리 / 툴팁 / 직렬화 / 범위.
	inline bool DrawPropertyMenu(ScriptSchema::Property& p, bool /*nameReadOnly*/)
	{
		bool changed = false;

		// 표시 이름 = Name("..") 어트리뷰트(인스펙터 표시용, C++ 멤버명과 별개. 항상 편집 가능).
		LabelWithTip("script_prop.display_name", "script_prop.display_name_tooltip");
		{
			ImInputText in; in.SetText(p.DisplayName); in.SetHintText(Loc::Text("script_prop.display_name_hint"));
			if (in(ImGuiInputTextFlags_None)) { p.DisplayName = static_cast<const char*>(in); changed = true; }
		}

		LabelWithTip("script_prop.category", "script_prop.category_tooltip");
		{
			ImInputText in; in.SetText(p.Category); in.SetHintText(Loc::Text("script_prop.category_hint"));
			if (in(ImGuiInputTextFlags_None)) { p.Category = static_cast<const char*>(in); changed = true; }
		}

		LabelWithTip("script_prop.tooltip", "script_prop.tooltip_tooltip");
		{
			ImInputText in; in.SetText(p.Tooltip); in.SetHintText(Loc::Text("script_prop.tooltip_hint"));
			if (in(ImGuiInputTextFlags_None)) { p.Tooltip = static_cast<const char*>(in); changed = true; }
		}

		ImGui::Separator();

		bool serialize = (false == p.NoSerialize);
		if (ImGui::Checkbox(Loc::Text("script_prop.serialize"), &serialize))
		{
			p.NoSerialize = (false == serialize); changed = true;
		}
		HoverTip("script_prop.serialize_tooltip");

		if (ImGui::Checkbox(Loc::Text("script_prop.range"), &p.HasRange)) changed = true;
		HoverTip("script_prop.range_tooltip");
		if (p.HasRange)
		{
			float mn = static_cast<float>(p.RangeMin);
			float mx = static_cast<float>(p.RangeMax);
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::DragFloat(Loc::Text("script_prop.range_min"), &mn, 0.1f)) { p.RangeMin = mn; changed = true; }
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::DragFloat(Loc::Text("script_prop.range_max"), &mx, 0.1f)) { p.RangeMax = mx; changed = true; }
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
		const float menuW      = ImGui::GetFrameHeight();           // ⋮ 정사각
		const bool  needsTarget= ScriptSchema::NeedsTargetCombo(p.TypeToken);
		const int   comboCount = needsTarget ? 2 : 1;              // 타입 (+Ref 대상)
		const int   slots      = comboCount + 1;                   // 콤보들 + 이름
		const float avail      = fullW - menuW - gap * static_cast<float>(slots);
		const float colW       = (avail > 0.0f) ? (avail / static_cast<float>(slots)) : 60.0f;

		changed |= TypeAndRefCombos(p, colW);

		// 이름
		ImGui::SameLine(0.0f, gap);
		ImGui::SetNextItemWidth(colW);
		{
			ImGui::BeginDisabled(nameReadOnly);
			ImInputText input;
			input.SetText(p.Name);
			input.SetHintText(Loc::Text("script_prop.name_hint"));
			if (input(ImGuiInputTextFlags_None, nameInvalid)) { p.Name = static_cast<const char*>(input); changed = true; }
			ImGui::EndDisabled();
		}
		HoverTip("script_prop.name_tooltip");

		// ⋮ 메뉴
		ImGui::SameLine(0.0f, gap);
		if (ImGui::Button("\xE2\x8B\xAE", ImVec2(menuW, 0.0f))) ImGui::OpenPopup("##prop_menu");   // U+22EE
		HoverTip("script_prop.menu_tooltip");
		if (ImGui::BeginPopup("##prop_menu"))
		{
			if (DrawPropertyMenu(p, nameReadOnly)) changed = true;
			ImGui::EndPopup();
		}
		return changed;
	}
}
