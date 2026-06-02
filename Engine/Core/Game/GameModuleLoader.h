#pragma once

#include "Core/Game/GameModuleTypes.h"
#include "Utillity/File/FilePath.h"

class IGameModule;

class CGameModuleLoader final
{
public:
	CGameModuleLoader();
	~CGameModuleLoader();

	bool LoadDynamicLibrary(const File::Path& libraryPath, const GameModuleContext& context);
	bool LoadStaticModule(CreateGameModuleFunc createFunc, DestroyGameModuleFunc destroyFunc, const GameModuleContext& context, const char* moduleName = nullptr);
	void Tick();
	void Unload();

	bool IsLoaded() const { return nullptr != m_gameModule; }
	IGameModule* GetGameModule() const { return m_gameModule; }
	const File::Path& GetLoadedPath() const { return m_loadedPath; }

private:
	void* m_libraryHandle = nullptr;
	IGameModule* m_gameModule = nullptr;
	DestroyGameModuleFunc m_destroyGameModule = nullptr;
	GameModuleHostApi m_hostApi;
	File::Path m_loadedPath;
};
