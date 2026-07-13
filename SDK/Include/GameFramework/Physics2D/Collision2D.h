#pragma once

#include "Utillity/Math/Vector2T.h"

class CGameObject;

// ─────────────────────────────────────────────────────────────────────────────
//  Collision2D — 충돌/트리거 이벤트가 스크립트로 전달하는 접촉 정보.
//
//  · Physics2DSystem 이 매 fixed step 종료 시 접촉 페어를 이전 프레임과 비교해
//    Enter/Stay/Exit 를 판정하고, 부착된 CGameScript 인스턴스의 훅으로 전달한다.
//  · 각 오브젝트 관점에서 채워진다: Other 는 상대 오브젝트, Normal 은 self→other
//    방향의 단위 접촉 법선이다(같은 충돌이라도 두 스크립트에 반대 부호로 전달).
//  · Exit 이벤트는 접촉이 끝난 시점이라 Normal/ContactPoint/Penetration 이
//    의미를 갖지 않으며 0 으로 채워진다.
//
//  ⚠ DLL 경계: 이 구조체는 게임 스크립트 훅 인자로 DLL 경계를 넘는다. 반드시 POD
//     여야 한다([[memory-pod-dll-boundary]]). Other 는 raw 포인터(프레임 내 풀 슬롯
//     주소 안정), Vector2 는 POD 이므로 안전하다.
// ─────────────────────────────────────────────────────────────────────────────
struct Collision2D
{
	// 충돌한 상대 오브젝트(이벤트를 받는 스크립트의 오너가 아닌 쪽).
	CGameObject* Other = nullptr;

	// self→other 방향 단위 접촉 법선. Exit 에서는 0.
	Vector2 Normal = Vector2(0.0f, 0.0f);

	// 대표 접촉점(월드 공간). 접촉점이 없거나 Exit 이면 0.
	Vector2 ContactPoint = Vector2(0.0f, 0.0f);

	// 공유 침투 깊이. Exit 이면 0.
	float Penetration = 0.0f;

	// 한쪽이라도 트리거 콜라이더이면 true(트리거 훅으로 전달된 이벤트).
	bool IsTrigger = false;
};
