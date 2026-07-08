#include "pch.h"
#include "ImFilterCombo.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/ImGuiUtillity.h"
#include "Editor/ImItem/ImItemLocalizationKeys.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <string_view>

namespace
{
	bool ContainsCaseInsensitive(std::string_view text, std::string_view filter)
	{
		if (filter.empty())
		{
			return true;
		}

		auto toLower = [](unsigned char c) -> char
		{
			return static_cast<char>(std::tolower(c));
		};

		return std::search(text.begin(), text.end(), filter.begin(), filter.end(),
			[&](char lhs, char rhs)
			{
				return toLower(static_cast<unsigned char>(lhs)) == toLower(static_cast<unsigned char>(rhs));
			}) != text.end();
	}
}

ImFilterCombo::ImFilterCombo(const char* id)
	: m_id(id)
{
}

ImFilterCombo& ImFilterCombo::Items(const std::vector<std::string>& items)
{
	m_items = &items;
	return *this;
}

ImFilterCombo& ImFilterCombo::CurrentIndex(int* index)
{
	m_currentIndex = index;
	return *this;
}

ImFilterCombo& ImFilterCombo::FilterHint(const char* text)
{
	m_filterHint = text;
	return *this;
}

ImFilterCombo& ImFilterCombo::EmptyText(const char* text)
{
	m_emptyText = text;
	return *this;
}

ImFilterCombo& ImFilterCombo::Width(float width)
{
	m_width = width;
	return *this;
}

ImFilterCombo& ImFilterCombo::MaxVisibleItems(int count)
{
	m_maxVisibleItems = count;
	return *this;
}

bool ImFilterCombo::Draw() const
{
	if (nullptr == m_items || nullptr == m_currentIndex)
	{
		return false;
	}

	if (0.0f != m_width)
	{
		ImGui::SetNextItemWidth(m_width);
	}

	const int itemCount = static_cast<int>(m_items->size());
	const char* preview = (*m_currentIndex >= 0 && *m_currentIndex < itemCount)
		? (*m_items)[static_cast<std::size_t>(*m_currentIndex)].c_str()
		: (nullptr != m_emptyText ? m_emptyText : "");

	bool changed = false;
	ImGui::Utillity::StyleBuilder comboStyle;
	comboStyle.PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
	if (ImGui::BeginCombo(nullptr != m_id ? m_id : "##filter_combo", preview))
	{
		static char filter[128] = "";
		if (ImGui::IsWindowAppearing())
		{
			filter[0] = '\0';
			ImGui::SetKeyboardFocusHere();
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint(
			"##filter",
			nullptr != m_filterHint ? m_filterHint : Loc::Text(ImItemLocKeys::CommonFilter),
			filter,
			sizeof(filter));
		ImGui::Separator();

		const int maxVisible = std::max(1, m_maxVisibleItems);
		const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
		const float childHeight = rowHeight * static_cast<float>(maxVisible);
		const float visibleHeight = std::min(childHeight, rowHeight * static_cast<float>(std::max(1, itemCount)));
		if (ImGui::BeginChild("##filter_combo_items", ImVec2(0.0f, visibleHeight), false))
		{
			for (int i = 0; i < itemCount; ++i)
			{
				const std::string& item = (*m_items)[static_cast<std::size_t>(i)];
				if (false == ContainsCaseInsensitive(item, filter))
				{
					continue;
				}

				const bool selected = (i == *m_currentIndex);
				if (ImGui::Selectable(item.c_str(), selected))
				{
					*m_currentIndex = i;
					changed = true;
				}
				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
		}
		ImGui::EndChild();
		ImGui::EndCombo();
	}

	return changed;
}

#endif
