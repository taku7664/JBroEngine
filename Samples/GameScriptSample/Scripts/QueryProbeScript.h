#pragma once

#include "GameFramework/Scripting/ScriptAPI.h"

// ── CQueryProbeScript ─────────────────────────────────────────────────────────
// 태그/이름 조회 + 물리 질의(Raycast/Overlap/Cast) 자가검증 스크립트.
// 아무 오브젝트에나 붙이고 재생하면, 스스로 **알려진 콜라이더 배치**를 만들어 놓고
// 모든 질의를 돌린 뒤 기대값과 대조해 PASS/FAIL 을 로그로 찍는다. 마지막 줄이 총평이다.
//
//   [QueryProbe] PASS  raycast wall distance                 expected 9.0000, got 9.0000
//   [QueryProbe] FAIL  boxcast rotated distance              expected 8.2929, got 8.5000
//   [QueryProbe] RESULT 31/32 PASS  (1 FAIL)
//
// 왜 씬을 손으로 안 만들고 스크립트가 짓는가:
//   질의 결과의 기대값은 콜라이더의 **정확한 월드 좌표**를 알아야 나온다. 에디터로 배치한
//   씬은 그 좌표가 저장 파일에 묻혀 있어 기대값을 코드로 못 적는다. 여기서는 배치도 기대값도
//   같은 파일에 있으므로 "무엇을 기준으로 맞다고 하는가"가 코드에 남는다.
//
// 격리:
//   · 프로브 콜라이더는 전용 충돌 레이어 비트(PROBE_LAYER)만 쓰고, 모든 질의에 그 마스크를
//     넘긴다 → 씬에 뭐가 있든 결과에 섞이지 않는다.
//   · CollisionMask 도 같은 비트로 좁혀 두어 씬 오브젝트와 **물리적으로도** 안 부딪힌다.
//   · 배치 원점을 멀리 잡아(PROBE_ORIGIN) 눈으로 볼 때도 본 게임과 겹치지 않는다.
//   · 검사가 끝나면 만든 오브젝트를 전부 파괴한다.
//
// 타이밍:
//   콜라이더의 WorldPoints/WorldAABB 는 Physics2DSystem 의 fixed step(UpdateColliderBounds)에서
//   채워진다. 생성 직후 질의하면 전부 빈 값이므로, 코루틴으로 fixed step 을 기다린 뒤 검사한다.
JBRO_SCRIPT CQueryProbeScript final : public CGameScript
{
	SCRIPT_CLASS(CQueryProbeScript)

protected:
	void OnStart() override;

private:
	Coroutine RunProbe();

	// 배치 — 만든 오브젝트는 m_spawned 에 모아 두었다가 마지막에 파괴한다.
	void BuildLayout();
	void TearDownLayout();

	CGameObject* SpawnBox(const char* name, const char* tag, const Vector2& position, const Vector2& size);
	CGameObject* SpawnCircle(const char* name, const char* tag, const Vector2& position, float radius);
	CGameObject* SpawnTwinCollider(const char* name, const char* tag, const Vector2& position);
	CGameObject* SpawnConcaveL(const char* name, const char* tag, const Vector2& position, float scale);

	// ── 검사 기록 ─────────────────────────────────────────────────────────────
	void Check(bool condition, const char* label);
	void CheckNear(float actual, float expected, const char* label);
	void CheckVector(const Vector2& actual, const Vector2& expected, const char* label);
	void CheckObject(const CGameObject* actual, const CGameObject* expected, const char* label);

	int m_passCount = 0;
	int m_failCount = 0;

	std::vector<SafePtr<CGameObject>> m_spawned;

	// 기대값 대조 대상 — 검사 중 유효해야 하므로 SafePtr 로 들고 있는다.
	SafePtr<CGameObject> m_wallNear;
	SafePtr<CGameObject> m_wallFar;
	SafePtr<CGameObject> m_ball;
	SafePtr<CGameObject> m_twin;
	SafePtr<CGameObject> m_concave;

	bool m_started = false;
};
