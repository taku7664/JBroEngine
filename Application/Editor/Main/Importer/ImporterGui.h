#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Localization/EditorLocalizationKeys.h"
#include "Engine/Editor/ImItem/ImText.h"
#include "Engine/Editor/ImGuiUtillity.h"

namespace ImporterGui
{
	template <typename DrawFunction>
	void DrawLocalizedRow(
		ImGui::Utillity::FormLayout& layout,
		const char* labelKey,
		const char* descriptionKey,
		DrawFunction&& drawFunction)
	{
		layout.Row(
			[&]() {
				ImText label;
				label.SetHoveredTooltip(Loc::Text(descriptionKey));
				label(Loc::Text(labelKey));
			},
			std::forward<DrawFunction>(drawFunction));
	}
}

#endif
