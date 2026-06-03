#include "pch.h"
#include "GameFramework/Object/Ref.h"

#include "Core/Asset/IAssetManager.h"
#include "Core/EngineCore.h"                         // 전역 Engine (BindEngineCore 로 채워짐)
#include "GameFramework/Component/ScriptComponent.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneManager.h"

namespace
{
	// Ref 해석의 기준 활성 씬.
	// ⚠️ 게임 DLL 에서는 Core::* 전역이 채워지지 않는다(BindEngineCore 는 전역 `Engine`
	//    [EngineCore] 만 호스트 값으로 복사함). 따라서 Core::SceneManager 가 아니라
	//    `Engine.SceneManager` 를 써야 DLL 에서도 동작한다.
	CScene* ActiveScene()
	{
		if (false == static_cast<bool>(Engine.SceneManager))
		{
			return nullptr;
		}
		return Engine.SceneManager->GetActiveScene().TryGet();
	}
}

// 경계엔 const char*(POD) 만 들어온다 — File::Guid 는 여기 Engine.lib 내부에서만 구성.

void* RefDetail::ResolveComponent(const char* objectGuid, const char* componentGuid, std::type_index componentType)
{
	CScene* scene = ActiveScene();
	if (nullptr == scene)
	{
		return nullptr;
	}
	CGameObject* object = scene->FindByInstanceGuid(File::Guid(objectGuid)).TryGet();
	return object ? object->FindComponentRawByGuid(File::Guid(componentGuid), componentType) : nullptr;
}

CGameObject* RefDetail::ResolveObject(const char* instanceGuid)
{
	CScene* scene = ActiveScene();
	if (nullptr == scene)
	{
		return nullptr;
	}
	return scene->FindByInstanceGuid(File::Guid(instanceGuid)).TryGet();
}

CGameScript* RefDetail::ResolveScript(const char* objectGuid, const char* componentGuid)
{
	CScene* scene = ActiveScene();
	if (nullptr == scene)
	{
		return nullptr;
	}
	CGameObject* object = scene->FindByInstanceGuid(File::Guid(objectGuid)).TryGet();
	if (nullptr == object)
	{
		return nullptr;
	}
	// 컴포넌트 guid 로 특정 ScriptComponent 지목(멀티 스크립트). guid 가 비어 있으면 첫 매치.
	const File::Guid compGuid(componentGuid);
	ScriptComponent* scriptComp = compGuid.IsNull()
		? object->GetComponent<ScriptComponent>()
		: dynamic_cast<ScriptComponent*>(object->FindComponentByGuid(compGuid));
	return scriptComp ? scriptComp->Instance : nullptr;
}

IAsset* RefDetail::ResolveAsset(const char* assetGuid)
{
	if (false == static_cast<bool>(Engine.AssetManager))
	{
		return nullptr;
	}
	const File::Guid guid(assetGuid);
	// 이미 로드돼 있으면 그것을, 아니면 로드한다(LoadAsset 은 멱등).
	// 주의: 여기서 반환되는 raw pointer 는 호출자가 strong ref 를 유지하지 않으므로
	// 자산이 unload 될 수 있다. 스크립트가 이 포인터를 매번 다시 받는다는 가정 하에 동작.
	AssetRef<IAsset> asset = Engine.AssetManager->FindLoadedAsset(guid);
	if (false == asset.IsValid())
	{
		asset = Engine.AssetManager->LoadAsset(guid);
	}
	return asset.Get();
}
