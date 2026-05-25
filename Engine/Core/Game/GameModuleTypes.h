#pragma once

class IAssetManager;
class IPlatform;
class IRHIDevice;
class IRenderSurface;
class IGameModule;
class CReflectionRegistry;

struct GameModuleDesc
{
	const char* Name = nullptr;
	const char* Version = nullptr;
};

struct GameModuleContext
{
	IPlatform*           Platform           = nullptr;
	IRenderSurface*      MainRenderSurface  = nullptr;
	IRHIDevice*          RHIDevice          = nullptr;
	IAssetManager*       AssetManager       = nullptr;
	// 스크립트 타입을 등록/해제할 레지스트리. 로드 시 채워집니다.
	CReflectionRegistry* Reflection         = nullptr;
};

using CreateGameModuleFunc = IGameModule*(*)();
using DestroyGameModuleFunc = void(*)(IGameModule* module);
