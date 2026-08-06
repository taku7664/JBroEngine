#pragma once

#include "Utillity/Math/Vector2T.h"
#include "Utillity/Types/ArrayView.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Physics2DQueryGeometry — 질의(레이/스윕/오버랩) 전용 볼록 지오메트리 프리미티브.
//
//  왜 별도 파일인가:
//    Physics2DSystem.cpp 의 익명 네임스페이스 헬퍼는 **솔버**(매니폴드 생성·침투 해소)용이라
//    "가장 깊은 침투"를 찾는다. 질의는 반대로 "가장 이른 접촉 시각"을 찾는다. 목적이 다르므로
//    같은 SAT 라도 코드가 다르고, 질의 쪽만 따로 쓰이는 곳이 늘어난다(에디터 피킹 등).
//
//  스윕의 원리 — Minkowski:
//    볼록 A 가 방향 d 로 이동하며 정지한 볼록 B 에 닿는 시각은
//    "A 의 기준점에서 d 방향으로 쏜 레이가 B ⊕ (−A) 에 닿는 시각" 과 같다.
//    · A 가 **원**이면 B ⊕ (−A) = B 를 반지름만큼 부풀린 도형 → RayVsRoundedConvex.
//    · A 가 **볼록 다각형**이면 Minkowski 합을 만드는 대신 축별 겹침 구간을 시간으로
//      환산하는 swept SAT 가 더 싸다 → SweptConvexVsConvex.
//    박스 스윕 vs 원은 관점을 뒤집어(원이 −d 로 이동) RayVsRoundedConvex 로 처리한다.
//
//  공통 계약:
//    · `direction` 은 **단위벡터**여야 한다(호출자가 정규화). 반환 `outT` 는 거리(월드 유닛).
//    · 시작 시점에 이미 겹쳐 있으면 `outT = 0`, `outNormal = -direction` 으로 보고한다.
//    · `outNormal` 은 맞은 도형 표면의 단위 법선이며 진행 방향 반대쪽을 향한다.
//    · 힙 할당을 하지 않는다. 입력은 ArrayView 라 std::vector 와 스택 배열 양쪽을 받는다.
//    · 폴리곤 입력은 **볼록**이어야 한다. 오목은 호출자가 볼록 조각으로 쪼개 넘긴다.
//      감기 방향(CW/CCW)은 신경 쓰지 않아도 된다 — 내부에서 부호 면적으로 판별한다.
// ─────────────────────────────────────────────────────────────────────────────
class CPhysics2DQueryGeometry final
{
public:
	// 레이 vs 원. 원 내부에서 시작하면 겹침으로 보고 outT = 0.
	static bool RayVsCircle(const Vector2& origin, const Vector2& direction, float maxDistance,
	                        const Vector2& center, float radius,
	                        float& outT, Vector2& outNormal);

	// 레이 vs "볼록 다각형을 radius 만큼 부풀린 도형"(라운드 볼록).
	// radius == 0 이면 순수 볼록 다각형이다. radius > 0 이면 반지름 radius 인 원을
	// direction 으로 스윕한 것과 같다.
	static bool RayVsRoundedConvex(const Vector2& origin, const Vector2& direction, float maxDistance,
	                               ArrayView<const Vector2> points, float radius,
	                               float& outT, Vector2& outNormal);

	// 볼록 A(direction 으로 maxDistance 까지 평행이동) vs 볼록 B(정지) — swept SAT.
	// outNormal 은 B 표면 기준(A 를 밀어내는 방향).
	static bool SweptConvexVsConvex(ArrayView<const Vector2> movingPoints,
	                                ArrayView<const Vector2> staticPoints,
	                                const Vector2& direction, float maxDistance,
	                                float& outT, Vector2& outNormal);

	// 정적 겹침 판정(불리언).
	static bool ConvexOverlapsConvex(ArrayView<const Vector2> a, ArrayView<const Vector2> b);
	static bool ConvexOverlapsCircle(ArrayView<const Vector2> points, const Vector2& center, float radius);

	// 회전 OBB 의 월드 공간 4점(CCW). rotationRadians 는 반시계 양수.
	static void BuildBoxPoints(const Vector2& center, const Vector2& halfExtents, float rotationRadians,
	                           Vector2 outPoints[4]);
};
