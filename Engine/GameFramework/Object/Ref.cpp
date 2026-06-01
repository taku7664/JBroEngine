#include "pch.h"
#include "GameFramework/Object/Ref.h"

#include "Core/Core.h"                              // Core::SceneManager / Core::AssetManager
#include "Core/Asset/IAssetManager.h"
#include "Core/Logging/LoggerInternal.h"             // [임시 진단] CSystemLog
#include "GameFramework/Component/ScriptComponent.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneManager.h"

#include <string>

namespace
{
	// Ref 해석의 기준이 되는 활성 씬. Core::SceneManager 는 DLL 에서도
	// BindEngineCore 로 호스트 것이 채워지므로 게임 스크립트에서도 동작한다.
	CScene* ActiveScene()
	{
		if (false == static_cast<bool>(Core::SceneManager))
		{
			return nullptr;
		}
		return Core::SceneManager->GetActiveScene().TryGet();
	}
}

// 경계엔 const char*(POD) 만 들어온다 — File::Guid(std::filesystem::path) 는 여기 Engine.lib
// 내부에서만 구성/사용하므로 호스트↔게임 DLL 사이 ABI 불일치가 발생하지 않는다.

void RefDetail::DiagGet(const char* guidBuf, bool isNull)
{
	CSystemLog::Info(std::string("[Ref] Get buf='") + (guidBuf ? guidBuf : "") + "' isNull=" + (isNull ? "1" : "0"));
}

void* RefDetail::ResolveComponent(const char* instanceGuid, std::type_index componentType)
{
	CScene* scene = ActiveScene();
	if (nullptr == scene)
	{
		CSystemLog::Info(std::string("[Ref] resolve: scene NULL guid=") + (instanceGuid ? instanceGuid : ""));
		return nullptr;
	}
	const EntityId entity = scene->FindEntityByInstanceGuid(File::Guid(instanceGuid));
	if (INVALID_ENTITY_ID == entity)
	{
		CSystemLog::Info(std::string("[Ref] resolve: entity NOT FOUND guid=") + (instanceGuid ? instanceGuid : ""));
		return nullptr;
	}
	void* comp = scene->GetComponentRaw(entity, componentType);
	CSystemLog::Info(std::string("[Ref] resolve: guid=") + instanceGuid
		+ " entity=" + std::to_string(static_cast<unsigned long long>(entity))
		+ " comp=" + (comp ? "OK" : "NULL") + " type=" + componentType.name());
	return comp;
}

CGameScript* RefDetail::ResolveScript(const char* instanceGuid)
{
	CScene* scene = ActiveScene();
	if (nullptr == scene)
	{
		return nullptr;
	}
	const EntityId entity = scene->FindEntityByInstanceGuid(File::Guid(instanceGuid));
	if (INVALID_ENTITY_ID == entity)
	{
		return nullptr;
	}
	// 엔티티당 ScriptComponent 는 단일(멀티 컴포넌트 비활성).
	ScriptComponent* scriptComp = scene->GetComponent<ScriptComponent>(entity);
	return scriptComp ? scriptComp->Instance : nullptr;
}

IAsset* RefDetail::ResolveAsset(const char* assetGuid)
{
	if (false == static_cast<bool>(Core::AssetManager))
	{
		return nullptr;
	}
	const File::Guid guid(assetGuid);
	// 이미 로드돼 있으면 그것을, 아니면 로드한다(LoadAsset 은 멱등).
	SafePtr<IAsset> asset = Core::AssetManager->FindLoadedAsset(guid);
	if (false == static_cast<bool>(asset))
	{
		asset = Core::AssetManager->LoadAsset(guid);
	}
	return asset.TryGet();
}
