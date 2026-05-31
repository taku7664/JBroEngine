#pragma once

#include "Core/Asset/IAssetRegistry.h"

#include <mutex>

// 워커 스레드에서 자산을 병렬 로드할 때 m_assetTable / m_pathToGuidTable 가
// 동시에 수정/조회되므로 m_mutex 로 보호.  Find* 가 const 멤버 포인터를 반환하는
// 인터페이스 특성상, 호출자는 반환된 포인터를 다른 mutate 호출 전에만 사용해야
// 한다 — 일반적인 한 박자 안의 read-then-use 패턴엔 안전.
class CAssetRegistry final : public IAssetRegistry
{
public:
	bool RegisterAsset(const AssetMetaData& metaData) override;
	bool UnregisterAsset(const AssetGuid& guid) override;
	void Clear() override;
	void ClearNonPersistent() override;
	const AssetMetaData* FindAsset(const AssetGuid& guid) const override;
	const AssetMetaData* FindAssetByPath(const File::Path& path) const override;
	void BuildSnapshot(AssetRegistrySnapshot& outSnapshot) const override;

private:
	mutable std::mutex m_mutex;
	std::unordered_map<AssetGuid, AssetMetaData> m_assetTable;
	std::unordered_map<File::Path, AssetGuid> m_pathToGuidTable;
};
