#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/ImItem/ImItemTypes.h"

class ImStatusBadge
{
public:
	explicit ImStatusBadge(const char* text);

	ImStatusBadge& Severity(EImValidationSeverity severity);
	ImStatusBadge& Tooltip(const char* text);
	ImStatusBadge& MinWidth(float width);

	void Draw() const;
	void operator()() const { Draw(); }

private:
	const char* m_text = nullptr;
	const char* m_tooltip = nullptr;
	EImValidationSeverity m_severity = EImValidationSeverity::Info;
	float m_minWidth = 0.0f;
};

#endif
