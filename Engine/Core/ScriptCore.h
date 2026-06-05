#pragma once

#include "Utillity/Pointer/SafePtr.h"

class CDebug;
class IAssetManager;
class IAudioDevice;
class CFileSystem;
class CInput;
class CLocalizationManager;
class CLogger;
class CReflectionRegistry;
class CSceneManager;
class CRandomService;
class CMathService;
class CTime;
class IDebugDraw2D;
class INetworkManager;

// ─────────────────────────────────────────────────────────────────────────────
//  ScriptCore — 게임 스크립트(유저)에게 노출하는 *엄선된* 공개 서비스 번들.
//
//  · EngineCore(엔진 내부 전체 API)의 부분집합이다. 스크립트가 정당하게 필요로 하는
//    게임플레이 서비스만 담는다. 렌더러/RHI/플랫폼/렌더서피스 등 엔진 내부 객체는
//    의도적으로 제외한다(스크립트가 GPU/플랫폼을 직접 만질 이유가 없다).
//  · DLL 경계를 넘는 유일한 번들. 호스트는 SyncScriptCore() 로 전역 `Engine` 에서
//    이 부분집합을 1회 복사하고, 게임 DLL 은 BindScriptCore() 로 호스트 값을 받는다.
//  · 따라서 DLL 안에서 도는 코드(스크립트 + DLL 에 링크되는 Engine.lib 코드 예: Ref.cpp)는
//    반드시 전역 `Script.X` 를 쓴다. 전역 `Engine`(EngineCore) 은 호스트에서만 채워진다.
// ─────────────────────────────────────────────────────────────────────────────
struct ScriptCore
{
	SafePtr<IAssetManager>        AssetManager;
	SafePtr<IAudioDevice>         Audio;
	SafePtr<CDebug>               Debug;
	SafePtr<CTime>                Time;
	SafePtr<CInput>               Input;
	SafePtr<CSceneManager>        SceneManager;
	SafePtr<CFileSystem>          FileSystem;
	SafePtr<CRandomService>       Random;
	SafePtr<CMathService>         Math;
	SafePtr<CReflectionRegistry>  Reflection;
	SafePtr<CLogger>              Logger;
	SafePtr<CLocalizationManager> Localization;
	SafePtr<INetworkManager>      Network;
	SafePtr<IDebugDraw2D>         DebugDraw2D;
};

inline ScriptCore Script;

void BindScriptCore(const ScriptCore* hostScriptCore);
void UnbindScriptCore();
