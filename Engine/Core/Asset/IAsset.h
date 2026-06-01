#pragma once

#include "Core/Asset/AssetTypes.h"
#include "Utillity/Pointer/SafePtr.h"

class IAsset : public EnableSafeFromThis<IAsset>
{
public:
	virtual ~IAsset() = default;

public:
	virtual AssetGuid GetGuid() const = 0;
	virtual EAssetType GetAssetType() const = 0;
	virtual EAssetLoadState GetLoadState() const = 0;
	virtual const AssetMetaData& GetMetaData() const = 0;
};

