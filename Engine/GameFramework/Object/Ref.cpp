#include "pch.h"
#include "GameFramework/Object/Ref.h"

#include "Core/Asset/IAssetManager.h"
#include "Core/ScriptCore.h"                         // 전역 Script (BindScriptCore 로 채워짐)
#include "GameFramework/Scripting/GameScript.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneManager.h"
#include "GameFramework/Scene/SceneRuntimeAccess.h"

namespace
{
	// Ref 해석의 기준 활성 씬.
	// ⚠️ 게임 DLL 에서는 전역 `Engine`(EngineCore) 이 채워지지 않는다 — BindScriptCore 는
	//    전역 `Script`(ScriptCore) 만 호스트 값으로 복사한다. 이 코드는 Engine.lib 이지만 게임
	//    DLL 에도 링크되어 DLL 안에서 돌므로, 반드시 `Script.SceneManager` 를 써야 한다.
	CGameScene* ActiveScene()
	{
		if (false == static_cast<bool>(Script.SceneManager))
		{
			return nullptr;
		}
		return Script.SceneManager->GetActiveScene().TryGet();
	}
}

// 경계엔 const char*(POD) 만 들어온다 — File::Guid 는 여기 Engine.lib 내부에서만 구성.

void* RefDetail::ResolveComponent(const char* objectGuid, const char* componentGuid, TypeId componentTypeId)
{
	CGameScene* scene = ActiveScene();
	if (nullptr == scene)
	{
		return nullptr;
	}
	CGameObject* object = scene->FindByInstanceGuid(File::Guid(objectGuid)).TryGet();
	return object
		? CSceneRuntimeAccess::FindComponentByGuidAndType(
			*object,
			File::Guid(componentGuid),
			componentTypeId)
		: nullptr;
}

CGameObject* RefDetail::ResolveObject(const char* instanceGuid)
{
	CGameScene* scene = ActiveScene();
	if (nullptr == scene)
	{
		return nullptr;
	}
	return scene->FindByInstanceGuid(File::Guid(instanceGuid)).TryGet();
}

CGameScript* RefDetail::ResolveScript(const char* objectGuid, const char* componentGuid, TypeId scriptTypeId)
{
	CGameScene* scene = ActiveScene();
	if (nullptr == scene)
	{
		return nullptr;
	}
	CGameObject* object = scene->FindByInstanceGuid(File::Guid(objectGuid)).TryGet();
	if (nullptr == object)
	{
		return nullptr;
	}
	return CSceneRuntimeAccess::FindScript(*scene, *object, File::Guid(componentGuid), scriptTypeId);
}

CGameScene* RefDetail::ResolveScene(const char* sceneName)
{
	if (false == static_cast<bool>(Script.SceneManager))
	{
		return nullptr;
	}
	// FindScene 은 헤더 인라인(GetActiveScene 과 동일한 DLL 링크 체인 사유).
	return Script.SceneManager->FindScene(sceneName).TryGet();
}

IAsset* RefDetail::ResolveAsset(const char* assetGuid)
{
	if (false == static_cast<bool>(Script.AssetManager))
	{
		return nullptr;
	}
	const File::Guid guid(assetGuid);
	// 이미 로드돼 있으면 그것을, 아니면 로드한다(LoadAsset 은 멱등).
	// 주의: 여기서 반환되는 raw pointer 는 호출자가 strong ref 를 유지하지 않으므로
	// 자산이 unload 될 수 있다. 스크립트가 이 포인터를 매번 다시 받는다는 가정 하에 동작.
	AssetRef<IAsset> asset = Script.AssetManager->FindLoadedAsset(guid);
	if (false == asset.IsValid())
	{
		asset = Script.AssetManager->LoadAsset(guid);
	}
	return asset.Get();
}
