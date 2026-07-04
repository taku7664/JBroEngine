#pragma once

#include "Utillity/File/FilePath.h" // File::Guid (안정 식별자)

// ─────────────────────────────────────────────────────────────────────────────
//  GameInstance — 씬에 존재하는 모든 실체(GameObject / Component)의 공통 베이스.
//
//  · 안정 식별자(InstanceGuid)를 통합 보유한다 — 씬 저장/로드, 슬롯 재배치, DLL
//    핫리로드를 넘어 대상을 지목하는 키(Ref<T> 가 사용).
//  · 수명 상태(생존/파괴대기/사망)는 차후 여기로 통합한다(GC 상태머신 단일 구현).
//  · 메모리 소유는 여전히 타입별 TObjectPool. 외부 안전참조는 파생 타입이 상속하는
//    EnableSafeFromThis<파생> 이 담당한다(이 베이스는 SafePtr 배관에 관여하지 않음).
//
//  ⚠ 이름 주의: Unreal 의 UGameInstance(게임 전역 싱글턴)와 개념이 다르다. 여기서는
//     "씬 실체의 공통 베이스"라는 뜻이다.
//  ⚠ DLL 경계: 이 베이스를 상속하는 것은 호스트 소유 객체(GameObject/빌트인 Component)
//     뿐이다. 게임 스크립트 인스턴스(CGameScript)는 상속하지 않는다(별도 DLL allocator).
// ─────────────────────────────────────────────────────────────────────────────
class GameInstance
{
public:
	virtual ~GameInstance() = default;

	// 안정 식별자. 생성 시 발급하고 직렬화가 보존한다(런타임 키가 아니라 영속 키).
	File::Guid InstanceGuid;
};
