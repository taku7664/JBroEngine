#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "ThirdParty/imgui/imgui.h"

enum class EImValidationSeverity
{
	Info,
	Success,
	Warning,
	Error
};

namespace ImItem
{
	bool IsEmptyText(const char* text);
	ImVec4 ValidationColor(EImValidationSeverity severity);
	const char* ValidationPrefix(EImValidationSeverity severity);
	ImVec4 WithAlpha(ImVec4 color, float alpha);
	ImVec4 ScaleColor(ImVec4 color, float scale);
	void HoveredTooltip(const char* text, ImGuiHoveredFlags flags = ImGuiHoveredFlags_None);
	void PushInvalidFrameStyle(bool invalid);
	void PopInvalidFrameStyle(bool invalid);
}

#endif
