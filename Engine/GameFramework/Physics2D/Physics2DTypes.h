#pragma once

#include "GameFramework/ECS/EntityTypes.h"
#include "Utillity/Math/Vector2T.h"

#include <cstdint>

enum class EPhysics2DBodyType
{
	Static,
	Kinematic,
	Dynamic
};

enum class EPhysics2DColliderType
{
	Polygon,
	Circle
};

struct PhysicsAABB2D
{
	Vector2 Min = Vector2(0.0f, 0.0f);
	Vector2 Max = Vector2(0.0f, 0.0f);
};

// 두 엔티티 사이의 충돌 매니폴드.
// ContactPoints / ContactCount : 실제 접촉점(최대 2개).
// Normal                       : A→B 방향 단위 법선.
// Penetration                  : 공유 침투 깊이 (접촉점마다 별도로 저장하지 않음).
//
// Position 보정은 반드시 매니폴드 단위(1회)로 적용해야 한다.
// 접촉점별로 반복 적용하면 과보정이 발생한다.
//
// ── Contact persistence (Box2D 식 warm-starting) ─────────────────────────────
// FeatureIds   : 각 contact 의 식별자. 다음 step 의 새 매니폴드와 매칭에 사용.
//                매칭되면 누적 impulse 를 복원하여 friction clamp 가
//                매 step 새로 시작되지 않고 누적값 기준으로 동작 → friction 정상.
// AccumulatedNormalImpulse / AccumulatedFrictionImpulse :
//   ResolveContactVelocity 가 매 iter delta impulse 를 누적, 다음 step warm-start 에 사용.
struct Physics2DManifold
{
	EntityId A = INVALID_ENTITY_ID;
	EntityId B = INVALID_ENTITY_ID;

	Vector2 Normal      = Vector2(0.0f, 0.0f);
	float          Penetration = 0.0f;

	Vector2 ContactPoints[2] = {};
	int            ContactCount     = 0;

	// Contact 식별자 — 같은 페어의 다음 step 매니폴드와 매칭에 사용.
	// Polygon: refFaceIdx * 1000 + incFaceIdx * 10 + clipFlag
	// Circle:  항상 0
	// Polygon-circle: 최근접 edge index
	// 매칭 안 되는 경우 (새 contact) 0xFFFFFFFF.
	std::uint32_t  FeatureIds[2]    = { 0xFFFFFFFFu, 0xFFFFFFFFu };

	// Warm-start 용 누적 impulse (이전 step 의 contact 와 매칭 시 복원).
	float          AccumulatedNormalImpulse[2]   = { 0.0f, 0.0f };
	float          AccumulatedFrictionImpulse[2] = { 0.0f, 0.0f };

	bool IsTrigger = false;
};
