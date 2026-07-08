#include "pch.h"
#include "ImItemTypes.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/ImGuiUtillity.h"

#include <algorithm>

namespace
{
	const ImVec4 LABEL_INVALID_COLOR(0.95f, 0.35f, 0.30f, 1.0f);
}

bool ImItem::IsEmptyText(const char* text)
{
	return nullptr == text || '\0' == text[0];
}

ImVec4 ImItem::ValidationColor(EImValidationSeverity severity)
{
	switch (severity)
	{
	case EImValidationSeverity::Success:
		return ImVec4(0.45f, 0.85f, 0.50f, 1.0f);
	case EImValidationSeverity::Warning:
		return ImVec4(0.95f, 0.75f, 0.35f, 1.0f);
	case EImValidationSeverity::Error:
		return ImVec4(0.95f, 0.35f, 0.30f, 1.0f);
	case EImValidationSeverity::Info:
	default:
		return ImVec4(0.65f, 0.75f, 0.95f, 1.0f);
	}
}

const char* ImItem::ValidationPrefix(EImValidationSeverity severity)
{
	switch (severity)
	{
	case EImValidationSeverity::Success:
		return "[OK] ";
	case EImValidationSeverity::Warning:
		return "[!] ";
	case EImValidationSeverity::Error:
		return "[X] ";
	case EImValidationSeverity::Info:
	default:
		return "[i] ";
	}
}

ImVec4 ImItem::WithAlpha(ImVec4 color, float alpha)
{
	color.w = alpha;
	return color;
}

ImVec4 ImItem::ScaleColor(ImVec4 color, float scale)
{
	color.x = std::clamp(color.x * scale, 0.0f, 1.0f);
	color.y = std::clamp(color.y * scale, 0.0f, 1.0f);
	color.z = std::clamp(color.z * scale, 0.0f, 1.0f);
	return color;
}

void ImItem::HoveredTooltip(const char* text, ImGuiHoveredFlags flags)
{
	if (IsEmptyText(text))
	{
		return;
	}

	ImGui::Utillity::HoveredToolTip(text, flags);
}

void ImItem::PushInvalidFrameStyle(bool invalid)
{
	if (false == invalid)
	{
		return;
	}

	ImGui::PushStyleColor(ImGuiCol_Border, LABEL_INVALID_COLOR);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
}

void ImItem::PopInvalidFrameStyle(bool invalid)
{
	if (false == invalid)
	{
		return;
	}

	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

#endif
