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
	// 결과는 **오브젝트 단위**다 — 한 오브젝트에 콜라이더가 여럿이어도 한 번만 들어간다.
	void OverlapCircle(const Vector2& center, float radius, std::vector<CGameObject*>& outResults,
	                   std::uint32_t layerMask = 0xffffffffu) const;

	// 레이 경로에 걸리는 **모든** 콜라이더를 거리 오름차순으로 outResults 에 채운다(먼저 clear).
	// Raycast 와 히트 판정은 완전히 같다 — 최근접 1개만 고르느냐 전부 모으느냐의 차이다.
	// 한 오브젝트에 콜라이더가 여럿이면 각각 별개 히트로 들어간다(관통 판정에 필요).
	void RaycastAll(const Vector2& origin, const Vector2& direction, float maxDistance,
	                std::vector<RaycastHit2D>& outHits, std::uint32_t layerMask = 0xffffffffu) const;

	// 회전 박스(OBB)와 겹치는 콜라이더들의 오브젝트. halfExtents 는 중심에서 각 축까지의 절반 크기,
	// rotationRadians 는 반시계 양수. OverlapCircle 과 같이 오브젝트 단위로 중복 제거된다.
	void OverlapBox(const Vector2& center, const Vector2& halfExtents, float rotationRadians,
	                std::vector<CGameObject*>& outResults, std::uint32_t layerMask = 0xffffffffu) const;

	// ── 스윕(모양 캐스트) ────────────────────────────────────────────────────
	// 도형을 direction 으로 maxDistance 만큼 밀면서 **처음 닿는** 콜라이더를 찾는다.
	// 스윕 중 도형은 회전하지 않는다. 출발 시점에 이미 겹쳐 있으면 Distance = 0,
	// Normal = -direction, Point = 도형 중심으로 보고한다(파고든 상태를 감추지 않기 위함).
	// Raycast 로는 못 하는 "폭을 가진 이동" 판정용이다 — 캐릭터 폭만큼의 지면/벽 체크 등.

	// 반지름 radius 의 원 스윕.
	bool CircleCast(const Vector2& origin, float radius, const Vector2& direction, float maxDistance,
	                RaycastHit2D& outHit, std::uint32_t layerMask = 0xffffffffu) const;

	// 회전 박스(OBB) 스윕.
	bool BoxCast(const Vector2& center, const Vector2& halfExtents, float rotationRadians,
	             const Vector2& direction, float maxDistance,
	             RaycastHit2D& outHit, std::uint32_t layerMask = 0xffffffffu) const;

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
	// 수집된 콜라이더에서 좁혀진 후보 쌍 목록(m_pairs*)을 만든다. DetectContacts 가 쌍마다
	// 정밀 판정을 돌리기 **전에** x축 sweep-and-prune 으로 명백히 안 겹치는 쌍을 걷어낸다.
	void BuildBroadPhasePairs();
	void DetectContacts(CGameCanvas& canvas);
	// ── 조인트 ───────────────────────────────────────────────────────────────
	// PrepareJoints: 스텝 시작 1회 — 자동 설정(쉬는 길이/연결점) 확정 + 워밍스타트 임펄스 적용.
	// SolveJointVelocity: 속도 반복 루프 안에서 접촉과 **함께** 수렴시킨다. 따로 풀면 조인트와
	//   접촉이 서로의 결과를 되돌린다(매달린 물체가 바닥에 닿는 순간 튀는 형태로 드러난다).
	void PrepareJoints(CGameCanvas& canvas, float deltaSeconds);
	void SolveJointVelocity(CGameCanvas& canvas, float deltaSeconds);

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

	// DetectContacts 가 활성 콜라이더를 모으는 버퍼. 이건 sub-step 마다 도는 경로라
	// (프레임당 최대 fixed step 8 × sub-step 4 = 32회) 지역 변수로 두면 그 횟수만큼
	// 할당 + push_back 성장 재할당이 붙는다.
	//
	// 볼록 여부를 여기 함께 담는 이유: 볼록/오목은 콜라이더 하나의 `WorldPoints` 만으로 정해지는
	// 성질인데, 예전에는 쌍 루프 **안에서** 매번 다시 판정했다(정점 전체 훑기). 쌍 수가 N² 이라
	// 총 비용이 O(N²·V) 가 된다. 수집할 때 콜라이더마다 한 번 재서 들고 다니면 O(N·V) 다.
	// `UpdateColliderBounds` 가 `DetectContacts` 앞에서 끝나고 쌍 루프는 기하를 읽기만 하므로,
	// 수집 시점의 판정이 그 스텝 내내 유효하다.
	struct DetectPolygon
	{
		CGameObject*       Owner    = nullptr;
		PolygonCollider2D* Collider = nullptr;
		bool               IsConvex = true;
	};
	std::vector<DetectPolygon>                              m_detectPolygons;
	std::vector<std::pair<CGameObject*, CircleCollider2D*>>  m_detectCircles;

	// ── 브로드페이즈(sweep-and-prune) ─────────────────────────────────────────
	// 예전에는 세 조합(폴리곤×폴리곤, 원×원, 폴리곤×원)을 전 쌍 순회하며 AABB 로 걸렀다.
	// 걸러도 **쌍을 방문하는 비용 자체가 O(N²)** 이고, 이 경로는 sub-step 마다 돈다.
	//
	// 대신 모든 콜라이더를 x축 구간(MinX~MaxX)으로 보고 MinX 순으로 훑는다. 훑는 동안
	// "아직 끝나지 않은" 구간만 active 에 남겨 두면, 현재 구간과 x 로 겹치는 것은 active
	// 안에 전부 있고 그 밖은 볼 필요가 없다. 겹침이 드문 보통 씬에서 방문 쌍이 급감한다.
	//
	// 세 종류를 한 번의 훑기로 처리하고 조합별 목록에 나눠 담는다 — 정밀 판정 코드는
	// 조합마다 완전히 다르므로 목록만 나누고 판정부는 그대로 둔다.
	struct SweepProxy
	{
		float         MinX     = 0.0f;
		float         MaxX     = 0.0f;
		std::uint32_t Index    = 0;       // m_detectPolygons / m_detectCircles 의 인덱스
		bool          IsCircle = false;
	};
	// 조합별 후보 쌍. A/B 는 해당 배열의 인덱스(폴리곤×원은 A=폴리곤, B=원).
	// 목록은 (A, B) 사전순으로 정렬해 둔다 — 전 쌍 순회 시절의 방문 순서와 같게 만들기 위해서다.
	// 매니폴드 생성 순서가 곧 솔버 순회 순서이고 Gauss-Seidel 반복은 순서에 의존하므로,
	// 훑는 순서(MinX 순, 오브젝트가 움직이면 프레임마다 뒤바뀜)를 그대로 흘리면 물리 결과가
	// 위치에 따라 미세하게 흔들린다. 정렬해 두면 쌍의 **집합**만 줄고 순서는 종전과 동일하다.
	struct BroadPhasePair
	{
		std::uint32_t A = 0;
		std::uint32_t B = 0;
	};
	// A 별 구간(위 목록이 A 로 정렬돼 있으므로 연속이다). 정밀 판정부가 "i 의 후보 j 들"을
	// 그대로 돌 수 있어 바깥 루프 구조를 바꾸지 않아도 된다.
	struct BroadPhaseRange
	{
		std::uint32_t Begin = 0;
		std::uint32_t Count = 0;
	};
	std::vector<SweepProxy>      m_sweepProxies;
	std::vector<SweepProxy>      m_sweepActive;
	std::vector<BroadPhasePair>  m_pairsPolygonPolygon;
	std::vector<BroadPhasePair>  m_pairsCircleCircle;
	std::vector<BroadPhasePair>  m_pairsPolygonCircle;
	std::vector<BroadPhaseRange> m_rangesPolygonPolygon;
	std::vector<BroadPhaseRange> m_rangesCircleCircle;
	std::vector<BroadPhaseRange> m_rangesPolygonCircle;

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
