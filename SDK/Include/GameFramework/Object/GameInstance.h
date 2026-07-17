#pragma once

#include "Utillity/File/FilePath.h" // File::Guid (안정 식별자)

// ─────────────────────────────────────────────────────────────────────────────
//  GameInstance — 캔버스에 존재하는 모든 실체(GameObject / Component)의 공통 베이스.
//
//  · 안정 식별자(InstanceGuid)를 통합 보유한다 — 캔버스 저장/로드, 슬롯 재배치, DLL
//    핫리로드를 넘어 대상을 지목하는 키(Ref<T> 가 사용).
//  · 수명 상태(생존/파괴대기/사망)는 차후 여기로 통합한다(GC 상태머신 단일 구현).
//  · 메모리 소유는 GameObject/빌트인 컴포넌트의 타입별 TObjectPool 또는 게임
//    스크립트의 타입 소거 풀이다. SafePtr 제어 블록은 각 생성 경로가 결합한다.
//
//  ⚠ 이름 주의: Unreal 의 UGameInstance(게임 전역 싱글턴)와 개념이 다르다. 여기서는
//     "캔버스 실체의 공통 베이스"라는 뜻이다.
//  ⚠ DLL 경계: CGameScript도 CComponent를 통해 이 베이스를 상속한다. 따라서 엔진과
//     게임 DLL은 동일 SDK 헤더와 호환되는 컴파일러/CRT 설정을 사용해야 한다.
// ─────────────────────────────────────────────────────────────────────────────
class GameInstance
{
public:
	virtual ~GameInstance() = default;
	const File::Guid& GetInstanceGuid() const { return m_instanceGuid; }

protected:
	GameInstance() = default;
	explicit GameInstance(const File::Guid& instanceGuid)
		: m_instanceGuid(instanceGuid)
	{
	}

private:
	friend class CGameCanvas;
	friend class CCanvasRuntimeAccess;

	void SetInstanceGuid(const File::Guid& instanceGuid) { m_instanceGuid = instanceGuid; }

	// 안정 식별자. 생성 시 발급하고 직렬화가 보존한다(런타임 키가 아니라 영속 키).
	File::Guid m_instanceGuid;
};
