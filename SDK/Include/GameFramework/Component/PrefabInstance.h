#pragma once

#include "Core/Asset/AssetTypes.h"
#include "GameFramework/Prefab/PrefabTypes.h"

#include <vector>

struct PrefabInstance
{
	AssetGuid SourcePrefabGuid = INVALID_ASSET_GUID;
	std::vector<PrefabPropertyOverride> PropertyOverrides;
};
