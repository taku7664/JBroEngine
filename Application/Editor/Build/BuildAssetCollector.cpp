#include "pch.h"
#include "BuildAssetCollector.h"

#include "Engine/Core/Asset/FontAsset.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/IAssetRegistry.h"
#include "Engine/GameFramework/Canvas/CanvasSerializer.h"

#include <queue>
#include <string>
#include <unordered_set>

namespace
{
	// 캔버스/프리팹/레이어/폰트패밀리처럼 하위 의존을 가진 타입만 파일을 열어 전개한다.
	// 그 외 타입은 리프이므로 파일 접근이 필요 없다.
	bool TypeHasDependencies(EAssetType type)
	{
		return EAssetType::Canvas == type
			|| EAssetType::Prefab == type
			|| EAssetType::Layer == type
			|| EAssetType::FontFamily == type;
	}

	std::vector<AssetGuid> ReadDependencies(EAssetType type, const File::Path& absolutePath)
	{
		if (EAssetType::FontFamily == type)
		{
			return CFontFamilyAsset::ReadDependencyGuids(absolutePath);
		}
		// Scene / Prefab / Layer — 셋 다 ReferencedAssets 시퀀스를 최상위에 두는 같은 규약이라
		// 오브젝트를 만들지 않고 그 키만 읽으면 된다.
		const CCanvasSerializer serializer;
		return serializer.ReadReferencedAssetsFromFile(absolutePath);
	}
}

BuildAssetCollectResult CBuildAssetCollector::Collect(IAssetManager& assetManager,
	const std::vector<AssetGuid>& seeds)
{
	BuildAssetCollectResult result;

	const IAssetRegistry& registry = assetManager.GetRegistry();

	std::unordered_set<std::string> visited;
	std::queue<AssetGuid> pending;
	for (const AssetGuid& seed : seeds)
	{
		if (false == seed.IsNull())
		{
			pending.push(seed);
		}
	}

	while (false == pending.empty())
	{
		const AssetGuid guid = pending.front();
		pending.pop();

		const std::string guidKey = guid.generic_string();
		if (false == visited.insert(guidKey).second)
		{
			continue;
		}

		AssetMetaData metaData;
		if (false == registry.TryGetAsset(guid, metaData))
		{
			result.Missing.push_back(guid);
			continue;
		}

		result.Included.push_back(guid);

		if (false == TypeHasDependencies(metaData.Type))
		{
			continue;
		}

		File::Path absolutePath;
		if (false == assetManager.ResolveAssetPath(metaData.Path, absolutePath))
		{
			// 하위 의존을 가진 자산인데 경로 해석 실패 → 전개 불가. 참조 무결성이 깨지므로
			// Missing 으로 보고해 빌드를 실패시킨다(누락을 조용히 넘기지 않는다).
			result.Missing.push_back(guid);
			continue;
		}

		for (const AssetGuid& dependency : ReadDependencies(metaData.Type, absolutePath))
		{
			if (false == dependency.IsNull() && visited.find(dependency.generic_string()) == visited.end())
			{
				pending.push(dependency);
			}
		}
	}

	return result;
}
