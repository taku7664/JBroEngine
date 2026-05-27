#pragma once

#include <cstddef>

class IAssetManager;
class IPlatform;
class IRHIDevice;
class IRenderSurface;
class IGameModule;
class CReflectionRegistry;
class CSceneManager;
class CLogger;
struct EngineCore;

using GameModuleAllocateFunc = void*(*)(std::size_t size, std::size_t alignment);
using GameModuleFreeFunc = void(*)(void* ptr, std::size_t size, std::size_t alignment);

struct GameModuleHostApi
{
	GameModuleAllocateFunc Allocate = nullptr;
	GameModuleFreeFunc Free = nullptr;
};

struct GameModuleDesc
{
	const char* Name = nullptr;
	const char* Version = nullptr;
};

struct GameModuleContext
{
	const EngineCore* HostEngine = nullptr;
};

using CreateGameModuleFunc = IGameModule*(*)(const GameModuleHostApi* hostApi);
using DestroyGameModuleFunc = void(*)(IGameModule* module, const GameModuleHostApi* hostApi);
