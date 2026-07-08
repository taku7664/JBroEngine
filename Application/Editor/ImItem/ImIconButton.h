#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "ThirdParty/imgui/imgui.h"

class ImIconButton
{
public:
	ImIconButton(const char* id, const char* icon);

	ImIconButton& Tooltip(const char* text);
	ImIconButton& Size(ImVec2 size);
	ImIconButton& Selected(bool selected = true);
	ImIconButton& Disabled(bool disabled = true);

	bool Draw() const;
	bool operator()() const { return Draw(); }

private:
	const char* m_id = nullptr;
	const char* m_icon = nullptr;
	const char* m_tooltip = nullptr;
	ImVec2 m_size = ImVec2(0.0f, 0.0f);
	bool m_selected = false;
	bool m_disabled = false;
};

#endif
