#pragma once

#include "Core/Game/IGameModule.h"
#include "GameScriptApi.h"

extern "C" GAMESCRIPT_API IGameModule* CreateGameModule(const GameModuleHostApi* hostApi);
extern "C" GAMESCRIPT_API void DestroyGameModule(IGameModule* module, const GameModuleHostApi* hostApi);

