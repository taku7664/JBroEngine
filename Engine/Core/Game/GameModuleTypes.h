#pragma once

class IAssetManager;
class IPlatform;
class IRHIDevice;
class IRenderSurface;
class IGameModule;

struct GameModuleDesc
{
	const char* Name = nullptr;
	const char* Version = nullptr;
};

struct GameModuleContext
{
	IPlatform* Platform = nullptr;
	IRenderSurface* MainRenderSurface = nullptr;
	IRHIDevice* RHIDevice = nullptr;
	IAssetManager* AssetManager = nullptr;
};

using CreateGameModuleFunc = IGameModule*(*)();
using DestroyGameModuleFunc = void(*)(IGameModule* module);
