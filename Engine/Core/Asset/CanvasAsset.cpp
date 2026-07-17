#include "pch.h"
#include "CanvasAsset.h"

// 캔버스 자산 타입은 아직 EAssetType::Canvas 이다 — 이름 자체(Scene→Canvas)는 코어
// 리네임 단계에서 한꺼번에 옮긴다. 클래스만 먼저 갈라 둔 건 Ref<CCanvasAsset> 이
// 그때까지 기다릴 이유가 없어서다.
CCanvasAssetLoader::CCanvasAssetLoader()
	: CFileAssetLoader(EAssetType::Canvas)
{
}

OwnerPtr<IAsset> CCanvasAssetLoader::CreateAsset(const AssetMetaData& metaData, std::vector<std::uint8_t>&& data) const
{
	return MakeOwnerPtr<CCanvasAsset>(metaData, std::move(data));
}
