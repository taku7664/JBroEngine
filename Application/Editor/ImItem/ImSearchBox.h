#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "ThirdParty/imgui/imgui.h"

#include <string>

class ImSearchBox
{
public:
	ImSearchBox(const char* id, std::string& text);

	ImSearchBox& Hint(const char* text);
	ImSearchBox& Width(float width);
	ImSearchBox& ShowClear(bool show = true);
	ImSearchBox& ClearTooltip(const char* text);
	ImSearchBox& Flags(ImGuiInputTextFlags flags);

	bool Draw() const;
	bool operator()() const { return Draw(); }

private:
	const char* m_id = nullptr;
	std::string& m_text;
	const char* m_hint = nullptr;
	float m_width = 0.0f;
	bool m_showClear = true;
	const char* m_clearTooltip = nullptr;
	ImGuiInputTextFlags m_flags = ImGuiInputTextFlags_None;
};

#endif
