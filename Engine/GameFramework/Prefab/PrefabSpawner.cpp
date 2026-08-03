#include "pch.h"
#include "PrefabSpawner.h"

#include "Core/Engine.h"
#include "Core/Asset/AssetRef.inl"        // LoadAsset 반환 AssetRef 의 복사/이동/소멸 인스턴스화
#include "Core/Asset/AssetTypeRules.h"
#include "Core/Asset/IAssetManager.h"
#include "Core/Asset/PrefabAsset.h"
#include "Core/Asset/TransientAssetLoad.h"
#include "Core/Logging/LoggerInternal.h"
#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Canvas/CanvasManager.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Serialization/ObjectSerializer.h"

const CPrefabSpawner::CachedPrefab* CPrefabSpawner::Acquire(const char* prefabAssetGuidText)
{
	if (nullptr == prefabAssetGuidText || '\0' == prefabAssetGuidText[0])
	{
		return nullptr;
	}

	// 캐시 조회는 정수 키로 — 여기까지가 스폰마다 도는 구간이라 힙 할당·문자열 비교가 없어야 한다.
	// AssetGuid(fs::path) 는 캐시 미스일 때만 만든다.
	const Guid128 key = Guid128::FromText(prefabAssetGuidText);
	for (const CachedPrefab& cached : m_cache)
	{
		if (cached.Guid == key)
		{
			return &cached;
		}
	}

	if (false == Engine.AssetManager.IsValid())
	{
		return nullptr;
	}

	const AssetGuid guid(prefabAssetGuidText);

	// 원문만 뽑아 오고 자산 자체는 도로 내린다 — 텍스트를 캐시로 복사해 두면 `.jprefab`
	// 자산을 계속 물고 있을 이유가 없다(캐시는 자동 GC 가 없어 놔두면 같은 바이트가
	// 두 벌 상주한다). CTransientAssetLoad 는 우리가 올린 것만 내리므로, 에디터 등
	// 다른 쪽이 이미 열어 둔 프리팹은 건드리지 않는다.
	CTransientAssetLoad load;
	const AssetRef<IAsset>& asset = load.Acquire(*Engine.AssetManager, guid);
	if (false == asset.IsValid())
	{
		CSystemLog::Error(std::string("Prefab spawn failed (asset not loaded - unregistered guid or unreadable file): ")
			+ prefabAssetGuidText);
		return nullptr;
	}
	if (EAssetType::Prefab != asset->GetAssetType())
	{
		CSystemLog::Error(std::string("Prefab spawn failed (asset is not a prefab): ") + prefabAssetGuidText
			+ ", type=" + CAssetTypeRules::GetTypeName(asset->GetAssetType()));
		return nullptr;
	}

	// 타입 enum 을 검증했으니 로더 등록 계약상 CPrefabAsset 이 확실하다 — 캐시 미스에서만
	// 도는 cold path 라 static_cast 로 내린다(dynamic_cast 아님).
	const CPrefabAsset* prefabFile = static_cast<const CPrefabAsset*>(asset.Get());
	if (nullptr == prefabFile)
	{
		return nullptr;
	}

	CachedPrefab entry;
	entry.Guid = key;
	// GetText() 는 널 종단이 보장되지 않는 바이트 뷰다 — std::string 으로 복사해야
	// 파서에 const char* 로 넘길 수 있다.
	entry.Text.assign(prefabFile->GetText());
	if (entry.Text.empty())
	{
		CSystemLog::Error(std::string("Prefab spawn failed (empty prefab file): ") + prefabAssetGuidText);
		return nullptr;
	}

	m_cache.push_back(std::move(entry));
	return &m_cache.back();
}

CGameObject* CPrefabSpawner::Spawn(const char* prefabAssetGuidText)
{
	const CachedPrefab* cached = Acquire(prefabAssetGuidText);
	if (nullptr == cached)
	{
		return nullptr;
	}

	if (false == Engine.CanvasManager.IsValid())
	{
		return nullptr;
	}
	CGameCanvas* canvas = Engine.CanvasManager->GetActiveCanvas().TryGet();
	if (nullptr == canvas)
	{
		return nullptr;
	}

	// InstanceGuid 는 파일 값 그대로 복원되지만, 캔버스에 같은 guid 가 살아 있으면
	// ObjectSerializer 가 새로 발급한다 — 같은 프리팹을 여러 번 스폰해도 guid 인덱스가
	// 충돌하지 않는 건 그 덕분이다.
	CGameObject* root = Serialization::DeserializeObject(*canvas, cached->Text.c_str());
	if (nullptr == root)
	{
		CSystemLog::Error(std::string("Prefab spawn failed (parse): ") + prefabAssetGuidText);
		return nullptr;
	}
	return root;
}

void CPrefabSpawner::ClearCache()
{
	m_cache.clear();
}
