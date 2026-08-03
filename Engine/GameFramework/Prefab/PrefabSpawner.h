#pragma once

#include "GameFramework/Prefab/IPrefabSpawner.h"
#include "Utillity/File/Guid128.h"

#include <deque>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  CPrefabSpawner — IPrefabSpawner 의 호스트 구현.
//
//  ⚠ 호스트 전용이다. 이 헤더/obj 는 게임 스크립트 DLL 에 들어가지 않는다 —
//    구현이 Serialization 계층(yaml-cpp)을 부른다. 스크립트는 IPrefabSpawner 만 본다.
//
//  프리팹 텍스트를 guid 별로 캐시한다. 캐시하지 않으면 총알 한 발마다 자산 조회 + 문자열
//  복사가 프레임 경로에서 반복된다. YAML **파싱**은 여전히 스폰마다 일어난다 — 파싱 결과를
//  재사용하려면 직렬화 계층에 중간 표현이 필요해서 이번 범위 밖으로 둔다.
// ─────────────────────────────────────────────────────────────────────────────
class CPrefabSpawner final : public IPrefabSpawner
{
public:
	using IPrefabSpawner::Spawn;   // 위치 지정 오버로드가 Spawn(const char*) 재정의에 가려지지 않게.

	CGameObject* Spawn(const char* prefabAssetGuidText) override;
	void ClearCache() override;

private:
	struct CachedPrefab
	{
		// 키가 `AssetGuid`(= File::Guid = fs::path 파생)가 아닌 이유: 스폰은 프레임 경로다.
		// path 로 키를 잡으면 스폰마다 경로 객체 생성(힙 할당)과 문자열 비교가 붙는다.
		// Guid128 은 원래 128비트를 그대로 담는 POD 라 생성·비교가 정수 연산 두 번이다.
		Guid128     Guid;
		std::string Text;   // `.jprefab` 원문 사본.
	};

	// guid 에 해당하는 캐시 항목을 돌려준다(없으면 자산을 읽어 채운다). 실패 시 nullptr.
	const CachedPrefab* Acquire(const char* prefabAssetGuidText);

	// deque 인 이유: push_back 이 기존 원소 주소를 무효화하지 않는다. Acquire 가 항목 포인터를
	// 돌려주는데, 그 포인터를 쓰는 도중 다른 프리팹이 캐시에 들어와도 안전해야 한다.
	// 항목 수는 프로젝트의 프리팹 종류 수준이고 조회는 스폰당 1회라 선형 탐색으로 충분하다.
	std::deque<CachedPrefab> m_cache;
};
