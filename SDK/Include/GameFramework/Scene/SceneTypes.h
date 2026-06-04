#pragma once

#include <cstdint>

enum class ESceneSerializeResult
{
	Success,
	InvalidArgument,
	IoError,
	ParseError
};

enum class ESceneSimulationState
{
	Edit,
	Playing,
	Paused,
};
