#pragma once

#include "Core/Asset/AssetTypes.h"
#include "GameFramework/Component/Component.h"
#include "GameFramework/Prefab/PrefabTypes.h"

#include <vector>

class PrefabInstance final : public CComponent
{
	JBRO_COMPONENT(PrefabInstance)
public:
	AssetGuid SourcePrefabGuid = INVALID_ASSET_GUID;
	std::vector<PrefabPropertyOverride> PropertyOverrides;
};
