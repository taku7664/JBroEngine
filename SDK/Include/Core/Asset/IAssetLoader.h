#pragma once

#include "Core/Asset/AssetTypes.h"
#include "Utillity/Pointer/SafePtr.h"

class IAsset;

class IAssetLoader : public EnableSafeFromThis<IAssetLoader>
{
public:
	virtual ~IAssetLoader() = default;

public:
	virtual EAssetType GetSupportedType() const = 0;
	virtual bool CanLoad(const AssetLoadDesc& desc) const = 0;
	virtual OwnerPtr<IAsset> Load(const AssetLoadDesc& desc) = 0;
	virtual void Unload(IAsset& asset) = 0;

	// in-place reload — 기존 자산 객체를 그대로 두고 디스크의 최신 상태로 데이터만 덮어쓴다.
	// 성공 시 true. false 면 AssetManager 가 fallback 으로 unload + load.
	// 기본 구현은 false — 옵션 갱신 외에 raw 변경까지 지원하지 않는 로더는 override 불필요.
	// 의미: 자산 객체 주소가 보존되므로 외부 AssetRef / 머티리얼 캐시가 죽지 않는다.
	virtual bool ReloadInto(IAsset& existing, const AssetMetaData& metaData) { (void)existing; (void)metaData; return false; }
};

