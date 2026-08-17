#include "pch.h"
#include "GameFramework/Object/Ref.h"

#include "Core/Asset/IAssetManager.h"
#include "Core/ScriptCore.h"                         // 전역 Script (BindScriptCore 로 채워짐)
#include "GameFramework/Scripting/GameScript.h"
#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Canvas/CanvasManager.h"
#include "GameFramework/Canvas/CanvasRuntimeAccess.h"

namespace
{
	// Ref 해석의 기준 활성 캔버스.
	// ⚠️ 게임 DLL 에서는 전역 `Engine`(EngineCore) 이 채워지지 않는다 — BindScriptCore 는
	//    전역 `Script`(ScriptCore) 만 호스트 값으로 복사한다. 이 코드는 Engine.lib 이지만 게임
	//    DLL 에도 링크되어 DLL 안에서 돌므로, 반드시 `Script.CanvasManager` 를 써야 한다.
	CGameCanvas* ActiveCanvas()
	{
		if (false == static_cast<bool>(Script.CanvasManager))
		{
			return nullptr;
		}
		return Script.CanvasManager->GetActiveCanvas().TryGet();
	}
}

// 경계엔 const char*(POD) 만 들어온다.
//
// 여기서 guid 텍스트를 File::Guid 로 만들지 않는다는 점이 중요하다 — File::Guid 는
// fs::path 라 생성이 곧 UTF8→UTF16 트랜스코딩 + 힙 할당이고, Get() 은 스크립트가 프레임마다
// 부를 수 있는 자리다. 대신 Guid128 로 접어 넘긴다(할당 0, 비교는 uint64 두 개).
// 오브젝트 조회는 FindByInstanceGuidText 가 이미 같은 방식이다.

void* RefDetail::ResolveComponent(const char* objectGuid, const char* componentGuid, TypeId componentTypeId)
{
	CGameCanvas* canvas = ActiveCanvas();
	if (nullptr == canvas)
	{
		return nullptr;
	}
	CGameObject* object = canvas->FindByInstanceGuidText(objectGuid).TryGet();
	return object
		? CCanvasRuntimeAccess::FindComponentByGuidAndType(
			*object,
			Guid128::FromText(componentGuid),
			componentTypeId)
		: nullptr;
}

CGameObject* RefDetail::ResolveObject(const char* instanceGuid)
{
	CGameCanvas* canvas = ActiveCanvas();
	if (nullptr == canvas)
	{
		return nullptr;
	}
	return canvas->FindByInstanceGuidText(instanceGuid).TryGet();
}

CGameScript* RefDetail::ResolveScript(const char* objectGuid, const char* componentGuid, TypeId scriptTypeId)
{
	CGameCanvas* canvas = ActiveCanvas();
	if (nullptr == canvas)
	{
		return nullptr;
	}
	CGameObject* object = canvas->FindByInstanceGuidText(objectGuid).TryGet();
	if (nullptr == object)
	{
		return nullptr;
	}
	return CCanvasRuntimeAccess::FindScript(*canvas, *object, Guid128::FromText(componentGuid), scriptTypeId);
}

CGameCanvas* RefDetail::ResolveCanvas(const char* canvasName)
{
	if (false == static_cast<bool>(Script.CanvasManager))
	{
		return nullptr;
	}
	// FindCanvas 은 헤더 인라인(GetActiveCanvas 과 동일한 DLL 링크 체인 사유).
	return Script.CanvasManager->FindCanvas(canvasName).TryGet();
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
