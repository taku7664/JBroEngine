#pragma once

#include "GameFramework/Scripting/ScriptAPI.h"

// ── CScreenSpaceProbeScript ───────────────────────────────────────────────────
// 화면 공간 레이어(ELayerSpace::Screen) 자가검증 스크립트.
//
// 필요한 자산이 없다 — 레이어와 오브젝트를 런타임에 만들고 끝나면 지운다.
//
// ── 무엇을 어떻게 보는가 ─────────────────────────────────────────────────────
// "카메라가 움직여도 UI 가 안 움직인다"를 스크립트에서 직접 보려면 렌더 결과가 아니라
// **역투영**을 봐야 한다. 월드 레이어 오브젝트의 World 행렬도 카메라와 무관하므로
// (월드 좌표는 원래 카메라를 모른다) 그것만 비교하면 아무것도 증명하지 못한다.
//
// 그래서 화면 중앙 픽셀을 두 방식으로 되돌려 본다:
//   · ScreenToWorld(중앙) → 카메라를 따라 움직여야 한다   (대조군 — 카메라가 실제로 움직였다는 증거)
//   · ScreenToUI(중앙)    → 언제나 (0,0) 이어야 한다      (본 검사)
// 대조군이 없으면 "카메라를 안 움직였는데 안 변했다"를 통과로 착각한다.
//
// 스케일 모드는 창을 리사이즈할 수 없으니 **가상의 렉트 크기**를 넣어 계산만 검사한다.
// 기대값은 구현을 호출해 만들지 않고 손으로 적은 식으로 세운다(자기 자신과 비교 금지).
//
// 앵커/월드 행렬 검사는 CTransformSystem 이 돈 뒤에 봐야 한다 — 시스템은 스크립트보다
// 나중에 도는 계약이라(Canvas::Update) 같은 프레임에 읽으면 지난 프레임 값이다.
JBRO_SCRIPT CScreenSpaceProbeScript final : public CGameScript
{
	SCRIPT_CLASS(CScreenSpaceProbeScript)

protected:
	void OnStart() override;

private:
	Coroutine RunProbe();

	void CheckExtents(float actualHalfW, float actualHalfH,
	                  float expectedHalfW, float expectedHalfH, const char* label);
	void Check(bool condition, const char* label);
	void CheckNear(float actual, float expected, float tolerance, const char* label);
	void CheckNearVector(const Vector2& actual, const Vector2& expected, float tolerance, const char* label);

	void TearDown();

	int m_passCount = 0;
	int m_failCount = 0;

	// 정리 대상 — 프로브가 만든 것만 지운다.
	std::vector<SafePtr<CGameObject>> m_spawned;
	SafePtr<CGameLayer>               m_screenLayer;

	bool m_started = false;
};
