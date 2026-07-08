#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/magic_enum/magic_enum.hpp"

#include <string>

template <typename E>
class ImEnumCombo
{
public:
	ImEnumCombo(const char* id, E& value)
		: m_id(id)
		, m_value(value)
	{
	}

	ImEnumCombo& Width(float width)
	{
		m_width = width;
		return *this;
	}

	bool Draw() const
	{
		if (0.0f != m_width)
		{
			ImGui::SetNextItemWidth(m_width);
		}

		const std::string current(magic_enum::enum_name(m_value));
		bool changed = false;
		if (ImGui::BeginCombo(nullptr != m_id ? m_id : "##enum_combo", current.c_str()))
		{
			for (const E candidate : magic_enum::enum_values<E>())
			{
				const bool selected = (candidate == m_value);
				const std::string name(magic_enum::enum_name(candidate));
				if (ImGui::Selectable(name.c_str(), selected))
				{
					m_value = candidate;
					changed = true;
				}
				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	bool operator()() const { return Draw(); }

private:
	const char* m_id = nullptr;
	E& m_value;
	float m_width = 0.0f;
};

#endif
