#pragma once

#include <cstdint>
#include <string>

#include "Core/Game/GameModuleTypes.h"

enum class ELiveCompileState
{
	Idle,
	Compiling,
	Loaded,
	Failed
};

struct LiveCompileDesc
{
	std::string SourceDirectory;
	std::string IntermediateDirectory;
	std::string OutputLibraryPath;
	std::string BuildCommand;
	float DebounceSeconds = 0.5f;
	GameModuleContext ModuleContext;
};

struct LiveCompileResult
{
	bool Succeeded = false;
	std::int32_t ExitCode = 0;
	std::string Message;
	std::string OutputLibraryPath;
};

