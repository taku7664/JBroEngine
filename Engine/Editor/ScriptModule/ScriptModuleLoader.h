#pragma once

#include "Core/Game/GameModuleTypes.h"
#include "Utillity/SafePtr.h"

class IDynamicLibrary;
class IGameModule;

// ── CScriptModuleLoader ───────────────────────────────────────────────────────
// 사전 빌드된 GameScript DLL을 단순 로드/언로드합니다.
// LiveCompileManager와 달리 소스 감시나 컴파일을 수행하지 않습니다.
// 사용자는 GameScript 프로젝트를 따로 빌드한 뒤 에디터에서 DLL을 불러옵니다.
//
// DLL이 내보내야 하는 함수:
//   extern "C" IGameModule* CreateGameModule(const GameModuleHostApi*);
//   extern "C" void         DestroyGameModule(IGameModule*, const GameModuleHostApi*);
class CScriptModuleLoader final
{
public:
	CScriptModuleLoader();
	~CScriptModuleLoader();

	// DLL을 로드하고 게임 모듈을 초기화합니다.
	// context.HostEngine에 유효한 EngineCore 포인터가 있어야 스크립트가 등록됩니다.
	bool Load(const char* dllPath, const GameModuleContext& context);

	// 게임 모듈을 정리하고 DLL을 언로드합니다.
	void Unload();

	bool        IsLoaded()       const { return nullptr != m_gameModule; }
	IGameModule* GetGameModule() const { return m_gameModule; }
	const char* GetLoadedPath()  const { return m_loadedPath.c_str(); }

private:
	OwnerPtr<IDynamicLibrary> m_library;
	IGameModule*              m_gameModule       = nullptr;
	DestroyGameModuleFunc     m_destroyGameModule = nullptr;
	GameModuleHostApi         m_hostApi;
	std::string               m_loadedPath;
};
