#pragma once

#include "GameFramework/Physics2D/Physics2DTypes.h"
#include "GameFramework/System/GameSystem.h"

class CPhysics2DSystem final : public CGameSystem
{
public:
	void SetGravity(const Vector2<float>& gravity);
	const Vector2<float>& GetGravity() const;

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
	void OnUpdate(CScene& scene) override;
	void OnFixedUpdate(CScene& scene) override;

private:
	void Step(CScene& scene, float deltaSeconds);
	void IntegrateBodies(CScene& scene, float deltaSeconds);
	void UpdateColliderBounds(CScene& scene);
	void DetectContacts(CScene& scene);
	void ResolveContactVelocity(CScene& scene);   // 속도 impulse — 접촉점별 (velocity only)
	void ResolveContactPosition(CScene& scene);   // 위치 보정    — 매니폴드별 1회
	void StabilizeRestingContacts(CScene& scene);
	void DrawManifoldDebugLines();                // 매니폴드 normal/contact 시각화 (fixed step 종료 후 1회)

	// 직전 step 의 매니폴드와 매칭해 누적 impulse 복원 + warm-start 적용.
	// DetectContacts 직후 호출되어 m_manifolds 의 AccumulatedXxxImpulse 를 prev 에서 복원,
	// 그 시점에 body 에 warm-start impulse 한 번 적용.
	void MatchAndWarmStart(CScene& scene);

private:
	Vector2<float>                  m_gravity             = Vector2<float>(0.0f, -9.8f);
	int                             m_velocityIterations  = 8;
	int                             m_positionIterations  = 2;
	int                             m_numSubSteps         = 4;
	float                           m_maxLinearVelocity   = 30.0f;
	std::vector<Physics2DManifold>  m_manifolds;
	// 직전 sub-step 의 매니폴드 — Contact persistence 매칭에 사용.
	std::vector<Physics2DManifold>  m_prevManifolds;
};
