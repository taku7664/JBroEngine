#include "pch.h"
#include "ScriptCore.h"

#include "Core/Logging/Logger.h"

void BindScriptCore(const ScriptCore* hostScriptCore)
{
	if (hostScriptCore)
	{
		Script = *hostScriptCore;
		Log::SetHostLogger(Script.Logger.TryGet());
		return;
	}

	UnbindScriptCore();
}

void UnbindScriptCore()
{
	Log::SetHostLogger(nullptr);
	Script = ScriptCore{};
}
