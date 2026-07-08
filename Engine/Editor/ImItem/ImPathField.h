#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "ThirdParty/imgui/imgui.h"

#include <string>

class ImPathField
{
public:
	ImPathField(const char* id, std::string& path);

	ImPathField& Hint(const char* text);
	ImPathField& Width(float width);
	ImPathField& ReserveTrailingWidth(float width);
	ImPathField& ReadOnly(bool readOnly = true);
	ImPathField& Invalid(bool invalid = true);
	ImPathField& Flags(ImGuiInputTextFlags flags);

	bool Draw() const;
	bool operator()() const { return Draw(); }

private:
	const char* m_id = nullptr;
	std::string& m_path;
	const char* m_hint = nullptr;
	float m_width = 0.0f;
	float m_reserveTrailingWidth = 0.0f;
	bool m_readOnly = true;
	bool m_invalid = false;
	ImGuiInputTextFlags m_flags = ImGuiInputTextFlags_None;
};

#endif
