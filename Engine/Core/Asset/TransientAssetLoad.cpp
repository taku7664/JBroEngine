#include "pch.h"
#include "TransientAssetLoad.h"

#include "Core/Asset/AssetRef.inl"   // AssetRef<IAsset> 복사/이동/소멸 인스턴스화
#include "Core/Asset/IAssetManager.h"

CTransientAssetLoad::~CTransientAssetLoad()
{
	Release();
}

const AssetRef<IAsset>& CTransientAssetLoad::Acquire(IAssetManager& manager, const AssetGuid& guid)
{
	// 같은 대상이면 아무것도 하지 않는다 — 매 프레임 그리는 인스펙터가 그대로 불러도 안전하다.
	if (m_manager == &manager && m_guid == guid && m_asset.IsValid())
	{
		return m_asset;
	}

	Release();

	if (guid.IsNull())
	{
		return m_asset;
	}

	// 우리가 올리기 **전에** 이미 캐시에 있었는지 본다. 있었다면 다른 사용자가 올려둔 것이므로
	// 나중에 내리지 않는다(남의 캐시를 치우면 그쪽이 재디코딩을 하게 된다).
	const bool alreadyCached = manager.FindLoadedAsset(guid).IsValid();

	m_asset = manager.LoadAsset(guid);
	if (false == m_asset.IsValid())
	{
		return m_asset;
	}

	m_manager = &manager;
	m_guid = guid;
	m_ownsCacheEntry = (false == alreadyCached);
	return m_asset;
}

void CTransientAssetLoad::Release()
{
	IAssetManager* manager = m_manager;
	const AssetGuid guid = m_guid;
	const bool unload = m_ownsCacheEntry && m_asset.IsValid() && nullptr != manager;

	// use-count 를 먼저 0 으로 만든다 — UnloadAsset 은 use-count > 0 이면 거부하므로
	// 순서가 뒤집히면 아무것도 안 내려간다(누수가 조용히 남는다).
	m_asset = {};
	m_manager = nullptr;
	m_guid = AssetGuid();
	m_ownsCacheEntry = false;

	if (unload)
	{
		manager->UnloadAsset(guid);
	}
}
