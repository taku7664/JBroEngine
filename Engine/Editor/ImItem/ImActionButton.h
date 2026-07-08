#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/ImItem/ImItemTypes.h"

class ImActionButton
{
public:
	explicit ImActionButton(const char* label);

	ImActionButton& Severity(EImValidationSeverity severity);
	ImActionButton& Tooltip(const char* text);
	ImActionButton& Size(ImVec2 size);
	ImActionButton& Disabled(bool disabled = true);

	bool Draw() const;
	bool operator()() const { return Draw(); }

private:
	const char* m_label = nullptr;
	const char* m_tooltip = nullptr;
	EImValidationSeverity m_severity = EImValidationSeverity::Info;
	ImVec2 m_size = ImVec2(0.0f, 0.0f);
	bool m_disabled = false;
};

#endif
