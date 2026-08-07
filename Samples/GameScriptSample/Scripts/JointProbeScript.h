#pragma once

#include "GameFramework/Scripting/ScriptAPI.h"

// ── CJointProbeScript ─────────────────────────────────────────────────────────
// 2D 조인트(DistanceJoint2D / HingeJoint2D) 자가검증 스크립트.
// 붙이고 재생하면 스스로 조인트 리그를 만들어 정해진 수의 고정 스텝만큼 굴린 뒤,
// 구속이 실제로 지켜졌는지 기대값과 대조해 PASS/FAIL 을 로그로 찍는다.
//
// 콜라이더를 **일부러 달지 않는다**. 조인트는 GetBodyWorldCenter 가 콜라이더가 없으면
// 트랜스폼 원점으로 폴백하므로 콜라이더 없이 성립하고, 그래야 접촉이 끼어들지 않아
// "조인트만" 검증된다. 배치 원점도 멀리 잡아 씬과 겹치지 않게 한다.
//
// 판정 기준: 조인트는 Baumgarte 바이어스로 푸는 소프트 구속이라 정확히 0 오차로 수렴하지
// 않는다. 허용오차는 "눈에 띄게 늘어지지 않았다" 수준으로 잡는다.
JBRO_SCRIPT CJointProbeScript final : public CGameScript
{
	SCRIPT_CLASS(CJointProbeScript)

protected:
	void OnStart() override;

private:
	Coroutine RunProbe();

	// 리그 조각 — 만든 오브젝트는 m_spawned 에 모았다가 마지막에 파괴한다.
	CGameObject* SpawnStatic(const char* name, const Vector2& position);
	CGameObject* SpawnDynamic(const char* name, const Vector2& position, bool useGravity);
	void         TearDown();

	void Check(bool condition, const char* label);
	void CheckNear(float actual, float expected, float tolerance, const char* label);

	int m_passCount = 0;
	int m_failCount = 0;

	std::vector<SafePtr<CGameObject>> m_spawned;
	bool m_started = false;
};
