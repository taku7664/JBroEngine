#pragma once

#include "GameFramework/Physics2D/Physics2DTypes.h"
#include "GameFramework/System/GameSystem.h"
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <vector>

class CGameObject;

class CPhysics2DSystem final : public CGameSystem
{
public:
	void SetGravity(const Vector2& gravity);
	const Vector2& GetGravity() const;

	// ── 질의 (스크립트/게임플레이용) ─────────────────────────────────────────
	// 콜라이더 월드 지오메트리(UpdateColliderBounds 산출)를 사용한다. 값은 최신 fixed
	// step 기준이라 가변 프레임 사이에는 최대 1 스텝 지연될 수 있다. 활성(IsActiveComponent)
	// 콜라이더만 대상이며 트리거/솔리드를 구분하지 않는다.
	// layerMask: (collider.CollisionLayer & layerMask) 가 0 이 아닌 콜라이더만 대상(기본 전체).

	// origin 에서 direction(정규화 불필요) 방향으로 maxDistance 까지 레이를 쏴 가장 가까운
	// 콜라이더를 찾는다. 맞으면 true + outHit 채움. direction 이 0 이면 false.
	bool Raycast(const Vector2& origin, const Vector2& direction, float maxDistance, RaycastHit2D& outHit,
	             std::uint32_t layerMask = 0xffffffffu) const;

	// point 를 포함하는 첫 번째 콜라이더의 오브젝트. 없으면 nullptr.
	CGameObject* OverlapPoint(const Vector2& point, std::uint32_t layerMask = 0xffffffffu) const;

	// 반지름 radius 의 원과 겹치는 콜라이더들의 오브젝트를 outResults 에 채운다(append 아님, 먼저 clear).
	void OverlapCircle(const Vector2& center, float radius, std::vector<CGameObject*>& outResults,
	                   std::uint32_t layerMask = 0xffffffffu) const;

	// Velocity-solver 반복 횟수.  높을수록 쌓인 물체 안정성/마찰 정확도↑, CPU↑.  기본: 8
	void SetVelocityIterations(int iterations);
	int  GetVelocityIterations() const;

	// Position-solver 반복 횟수.  Baumgarte가 대부분 처리하므로 보조(잔여 오차) 역할.  기본: 2
	void SetPositionIterations(int iterations);
	int  GetPositionIterations() const;

	// 고정 스텝을 N등분하는 sub-step 수.  높을수록 스텝당 초기 침투량↓, CPU×N.  기본: 4
	void SetNumSubSteps(int steps);
	int  GetNumSubSteps() const;

	// 터널링 방지 최대 속도 (m/s).  한 서브스텝에서 MaxLinearVelocity × sub_dt 이상 이동 불가.
	// 기본: 30.0f (sub_dt=0.005s 기준 → 최대 이동 = 0.15 유닛, PPU=100이면 15px).
	void  SetMaxLinearVelocity(float maxVel);
	float GetMaxLinearVelocity() const;

	// 현재 프레임에서 감지된 충돌 매니폴드 목록.
	const std::vector<Physics2DManifold>& GetManifolds() const;

protected:
	// 질의가 canvas 참조 없이 동작하도록 초기화 시 canvas 포인터를 캐싱한다(시스템 수명=캔버스 수명).
	void OnInitialize(CGameCanvas& canvas) override;
	void OnFixedUpdate(CGameCanvas& canvas) override;
	// 시뮬레이션 정지 시 접촉 상태 초기화 — 재생 재개 시 잔여 Exit 이벤트가 튀지 않게 한다.
	void OnSimulationStop(CGameCanvas& canvas) override;

private:
	void Step(CGameCanvas& canvas, float deltaSeconds);
	void IntegrateBodies(CGameCanvas& canvas, float deltaSeconds);
	void UpdateColliderBounds(CGameCanvas& canvas);
	void DetectContacts(CGameCanvas& canvas);
	void ResolveContactVelocity(CGameCanvas& canvas);   // 속도 impulse — 접촉점별 (velocity only)
	void ResolveContactPosition(CGameCanvas& canvas);   // 위치 보정    — 매니폴드별 1회
	void StabilizeRestingContacts(CGameCanvas& canvas);
	void DrawManifoldDebugLines();                // 매니폴드 normal/contact 시각화 (fixed step 종료 후 1회)

	// 이번 fixed step 의 접촉 페어를 직전 step 과 비교해 Enter/Stay/Exit 를 판정하고,
	// 부착된 CGameScript 인스턴스의 충돌/트리거 훅으로 전달한다(fixed step 종료 후 1회).
	void DispatchContactEvents(CGameCanvas& canvas);

	// 충돌/트리거 훅 디스패치 헬퍼. CGameScript 의 훅 진입점(CollisionEnter 등)은 private 이고
	// 이 시스템이 friend 이므로, 접근하려면 자유함수가 아니라 멤버여야 한다.
	enum class EContactPhase { Enter, Stay, Exit };
	static void DispatchToScript(CGameObject* self, CGameObject* other, const Vector2& normal,
	                             const Vector2& contactPoint, float penetration, bool isTrigger, EContactPhase phase);
	static void DispatchPair(CGameObject* a, CGameObject* b, const Vector2& normalAtoB,
	                         const Vector2& contactPoint, float penetration, bool isTrigger, EContactPhase phase);

	// 직전 step 의 매니폴드와 매칭해 누적 impulse 복원 + warm-start 적용.
	// DetectContacts 직후 호출되어 m_manifolds 의 AccumulatedXxxImpulse 를 prev 에서 복원,
	// 그 시점에 body 에 warm-start impulse 한 번 적용.
	void MatchAndWarmStart(CGameCanvas& canvas);

private:
	Vector2                  m_gravity             = Vector2(0.0f, -9.8f);
	int                             m_velocityIterations  = 8;
	int                             m_positionIterations  = 2;
	int                             m_numSubSteps         = 4;
	float                           m_maxLinearVelocity   = 30.0f;
	std::vector<Physics2DManifold>  m_manifolds;
	// 직전 sub-step 의 매니폴드 — Contact persistence 매칭에 사용.
	std::vector<Physics2DManifold>  m_prevManifolds;

	// 직전 fixed step 에서 접촉 중이던 오브젝트 페어(정규화: A 주소 < B 주소).
	// Enter/Stay/Exit 판정에 사용. SafePtr 로 보관해 오브젝트가 파괴돼도 dangling 을 피한다.
	struct ContactPairState
	{
		SafePtr<CGameObject> A;
		SafePtr<CGameObject> B;
		bool                 IsTrigger = false;
	};
	std::vector<ContactPairState>   m_prevContacts;

	// ── 프레임 스크래치 ──────────────────────────────────────────────────────
	// 아래 컨테이너들은 매 fixed step(프레임당 0~8회) · 매 sub-step(스텝당 4회) 도는
	// 경로에서 쓰인다. 지역 변수로 두면 그 횟수만큼 힙 할당이 발생하므로 멤버로 올려
	// clear() 로 용량을 재사용한다(프레임 루프 힙 할당 금지 규칙).

	// DispatchContactEvents 가 이번 스텝의 접촉 페어를 모으는 버퍼.
	struct CurrentContact
	{
		CGameObject* A = nullptr;
		CGameObject* B = nullptr;
		Vector2      Normal = Vector2(0.0f, 0.0f);
		Vector2      Point  = Vector2(0.0f, 0.0f);
		float        Penetration = 0.0f;
		bool         IsTrigger   = false;
	};
	std::vector<CurrentContact>     m_currentContacts;

	// 같은 (A,B) 페어 매니폴드 병합용 키. 포인터 두 개를 좁은 정수로 패킹하면 비트가 겹쳐
	// 서로 다른 페어가 충돌하므로, 키는 원본을 들고 해시만 결합한다.
	struct ManifoldPairKey
	{
		std::uintptr_t Lo = 0;
		std::uintptr_t Hi = 0;
		bool operator==(const ManifoldPairKey&) const = default;
	};

	// 병합 인덱스 — unordered_map 이 아니라 오픈 어드레싱 테이블이다. map 은 clear() 해도
	// 원소마다 노드를 다시 할당하는데, 이 경로는 sub-step 마다 매니폴드 수만큼 도는 자리라
	// 프레임당 수천 번의 힙 할당이 된다. 테이블은 멤버로 재사용하므로 용량이 찬 뒤엔 0 할당이다.
	// (범용 해시맵을 새로 만들지 않는 건 사용처가 여기 하나뿐이기 때문이다.)
	static constexpr std::uint32_t INVALID_MERGE_SLOT = 0xFFFFFFFFu;
	struct ManifoldMergeSlot
	{
		ManifoldPairKey Key;
		std::uint32_t   MergedIndex = INVALID_MERGE_SLOT;
	};
	// 키를 테이블 슬롯으로 — 사용처가 하나라 멤버 함수로 둔다(별도 해시 타입 불필요).
	static std::size_t HashManifoldPairKey(const ManifoldPairKey& key);
	// 최소 minSlots 이상·2의 거듭제곱 크기로 테이블을 비운다(선형 탐사 종료 보장용 여유 포함).
	void ResetManifoldMergeTable(std::size_t minSlots);
	// 키가 있으면 merged 인덱스를, 없으면 INVALID_MERGE_SLOT 을 돌려주고 outSlot 에 삽입 위치를 준다.
	std::uint32_t FindManifoldMergeSlot(const ManifoldPairKey& key, std::size_t& outSlot) const;

	std::vector<ManifoldMergeSlot>  m_manifoldMergeTable;
	std::vector<Physics2DManifold>  m_mergedManifolds;

	// 질의용 canvas 캐시(OnInitialize 에서 설정). 시스템은 캔버스가 소유하므로 수명 내 유효.
	CGameCanvas*                     m_canvas = nullptr;
};
