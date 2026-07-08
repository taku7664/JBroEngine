#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/ImItem/ImItemTypes.h"

class ImValidationMessage
{
public:
	ImValidationMessage(const char* message, EImValidationSeverity severity = EImValidationSeverity::Info);

	ImValidationMessage& Wrap(bool wrap = true);

	void Draw() const;
	void operator()() const { Draw(); }

private:
	const char* m_message = nullptr;
	EImValidationSeverity m_severity = EImValidationSeverity::Info;
	bool m_wrap = true;
};

#endif
