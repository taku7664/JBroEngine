#pragma once

#include "Core/Asset/AssetTypes.h"

#include <vector>

class IAssetManager;

// 참조 기반 패키징을 위한 자산 수집기.
//
// 시드(빌드 씬 / 스타트업 씬 / Always Include / 기본·폴백 폰트 패밀리)에서 출발해
// 전이 의존을 BFS 로 전개한다:
//   · Scene / Prefab  → 파일의 ReferencedAssets (프리팹은 씬과 동일 포맷)
//   · FontFamily      → 스타일별 FontFace + FallbackFamilies (패밀리는 다시 전개)
//   · 그 외 타입       → 리프(추가 의존 없음)
//
// 레지스트리에 없는 GUID 나 경로 해석 실패는 Missing 으로 모은다 — 호출부(빌드)가
// 이를 근거로 빌드를 실패시켜 "참조 누락을 조용히 넘기지 않는다"는 정책을 강제한다.
struct BuildAssetCollectResult
{
	std::vector<AssetGuid> Included;   // 패키지에 넣을 GUID(중복 제거됨)
	std::vector<AssetGuid> Missing;    // 참조됐지만 레지스트리에 없거나 경로 해석 실패
};

class CBuildAssetCollector
{
public:
	static BuildAssetCollectResult Collect(IAssetManager& assetManager,
		const std::vector<AssetGuid>& seeds);
};
