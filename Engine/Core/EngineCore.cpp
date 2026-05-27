#include "pch.h"
#include "EngineCore.h"

#include "Core/Logging/Logger.h"

void BindEngineCore(const EngineCore* hostEngine)
{
	if (hostEngine)
	{
		Engine = *hostEngine;
		Log::SetHostLogger(Engine.Logger.TryGet());
		return;
	}

	UnbindEngineCore();
}

void UnbindEngineCore()
{
	Log::SetHostLogger(nullptr);
	Engine = EngineCore{};
}
