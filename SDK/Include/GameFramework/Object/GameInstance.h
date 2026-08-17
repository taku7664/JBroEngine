#pragma once

#include "Utillity/File/FilePath.h" // File::Guid (안정 식별자)
#include "Utillity/File/Guid128.h"  // Guid128 (조회용 정규형)

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

	// 같은 식별자의 조회용 정규형. "이 guid 가 맞나"만 묻는 자리는 이쪽을 쓴다 —
	// Ref 해석처럼 프레임마다 도는 경로에서 File::Guid 비교는 fs::path 비교이고, 비교할
	// 값을 만드는 것 자체가 UTF16→UTF8 트랜스코딩 + 힙 할당이다. Guid128 은 uint64 두 개다.
	// 두 값은 AssignInstanceGuid 에서만, 항상 함께 갱신된다.
	const Guid128& GetInstanceGuid128() const { return m_instanceGuid128; }

protected:
	GameInstance() = default;
	explicit GameInstance(const File::Guid& instanceGuid)
	{
		AssignInstanceGuid(instanceGuid);
	}

private:
	friend class CGameCanvas;
	friend class CCanvasRuntimeAccess;

	void SetInstanceGuid(const File::Guid& instanceGuid) { AssignInstanceGuid(instanceGuid); }

	// 두 표현이 갈라지지 않게 쓰기를 여기 하나로 모은다 — 갈라지면 Ref 가 조용히 대상을
	// 못 찾는다(문자열은 맞는데 정수형이 옛 값). 호출은 생성/로드 시점뿐이라 변환 비용은 무관.
	// 정의는 GameInstance.cpp — 인라인이면 안 되는 이유가 그 파일에 적혀 있다.
	void AssignInstanceGuid(const File::Guid& instanceGuid);

	// 안정 식별자. 생성 시 발급하고 직렬화가 보존한다(런타임 키가 아니라 영속 키).
	File::Guid m_instanceGuid;
	Guid128    m_instanceGuid128;
};
