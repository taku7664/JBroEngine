#pragma once

#include "Core/Platform/PlatformDefines.h"

#if !JBRO_PLATFORM_WINDOWS
#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(parameter) (void)(parameter)
#endif
#endif

struct PlatformDesc
{
	const wchar_t* ApplicationName = L"JBroEngine";
	int WindowWidth = 1280;
	int WindowHeight = 720;
	bool IsEditor = false;
};

struct PlatformEvent
{
	bool WantsExit = false;
	bool IsFocused = true;
	bool FocusGained = false;
	bool FocusLost = false;
};

enum class EPlatformType
{
	Unknown,
	Windows,
	Web
};
