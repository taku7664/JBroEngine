#pragma once

#include "GameFramework/Component/Component.h"
#include "GameFramework/Object/Ref.h"
#include "Utillity/Math/Vector2T.h"

// ─────────────────────────────────────────────────────────────────────────────
//  2D 조인트 — 두 강체(또는 강체와 월드의 한 점)를 잇는 구속.
//
//  CPhysics2DSystem 이 접촉과 **같은 속도 반복 루프** 안에서 푼다. 접촉이 시퀀셜 임펄스로
//  풀리므로 조인트도 같은 방식이어야 서로 밀어내지 않는다.
//
//  ── 앵커 ────────────────────────────────────────────────────────────────────
//  LocalAnchor 는 이 오브젝트의 **로컬 좌표**, ConnectedAnchor 는 상대 오브젝트의 로컬 좌표다.
//  ConnectedObject 가 비어 있으면 ConnectedAnchor 를 **월드 좌표**로 해석한다 — 진자를 천장에
//  매다는 것처럼 움직이지 않는 한 점에 묶는 흔한 경우를 위해서다.
//
//  ── 위치 보정 ───────────────────────────────────────────────────────────────
//  조인트는 별도 위치 패스를 두지 않고 속도 구속에 Baumgarte 바이어스를 섞어 푼다.
//  접촉의 위치 패스와 조인트의 위치 패스가 서로를 되돌리는 진동을 피하기 위한 선택이다.
//  대신 강한 구속에서 약간의 늘어짐(soft) 이 남을 수 있다.
// ─────────────────────────────────────────────────────────────────────────────

class CGameObject;

// 두 앵커 사이 거리를 유지한다. 막대/로프/스프링이 전부 이 하나로 표현된다.
class DistanceJoint2D final : public CComponent
{
	JBRO_COMPONENT(DistanceJoint2D)
public:
	// 비우면 ConnectedAnchor 를 월드 고정점으로 본다.
	Ref<CGameObject> ConnectedObject;

	Vector2 LocalAnchor     = Vector2(0.0f, 0.0f);
	Vector2 ConnectedAnchor = Vector2(0.0f, 0.0f);

	// true 면 시뮬레이션이 처음 이 조인트를 볼 때의 실제 거리를 Distance 로 삼는다.
	// 에디터에서 배치한 그대로가 곧 쉬는 길이가 된다.
	bool  AutoConfigureDistance = true;
	float Distance = 1.0f;

	// true 면 "이보다 멀어지지만 않게" 한다(로프). 가까워지는 건 자유.
	bool  MaxDistanceOnly = false;

	// 0 이면 강체 구속. 0 보다 크면 스프링이며 값은 진동수(Hz)다.
	// DampingRatio 1.0 = 임계 감쇠(진동 없이 수렴), 0 = 감쇠 없음.
	float Frequency    = 0.0f;
	float DampingRatio = 0.7f;

	// ── 런타임 상태(리플렉션/직렬화 제외) ────────────────────────────────────
	// 워밍스타트용 누적 임펄스. 매 스텝 유지되어야 반복 횟수가 적어도 늘어지지 않는다.
	float RuntimeAccumulatedImpulse = 0.0f;
	// AutoConfigureDistance 를 1회만 적용하기 위한 플래그.
	bool  RuntimeConfigured = false;
};

// 두 앵커를 한 점에 붙인다(회전은 자유). 문 경첩·바퀴 축·래그돌 관절.
class HingeJoint2D final : public CComponent
{
	JBRO_COMPONENT(HingeJoint2D)
public:
	// 비우면 ConnectedAnchor 를 월드 고정점으로 본다.
	Ref<CGameObject> ConnectedObject;

	Vector2 LocalAnchor     = Vector2(0.0f, 0.0f);
	Vector2 ConnectedAnchor = Vector2(0.0f, 0.0f);

	// true 면 시뮬레이션 시작 시 LocalAnchor 의 월드 위치가 곧 연결점이 되도록
	// ConnectedAnchor 를 역산한다. 두 오브젝트를 겹쳐 놓고 축만 찍으면 되게.
	bool AutoConfigureConnectedAnchor = true;

	// ── 런타임 상태(리플렉션/직렬화 제외) ────────────────────────────────────
	// 점 구속은 2 자유도라 누적 임펄스도 2차원이다.
	Vector2 RuntimeAccumulatedImpulse = Vector2(0.0f, 0.0f);
	bool    RuntimeConfigured = false;
};
