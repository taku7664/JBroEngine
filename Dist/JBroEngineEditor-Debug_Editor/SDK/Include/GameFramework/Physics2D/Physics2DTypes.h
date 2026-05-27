#pragma once

#include "GameFramework/ECS/EntityTypes.h"
#include "Utillity/Vector2T.h"

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
	Vector2<float> Min = Vector2<float>(0.0f, 0.0f);
	Vector2<float> Max = Vector2<float>(0.0f, 0.0f);
};

// 두 엔티티 사이의 충돌 매니폴드.
// ContactPoints / ContactCount : 실제 접촉점(최대 2개).
// Normal                       : A→B 방향 단위 법선.
// Penetration                  : 공유 침투 깊이 (접촉점마다 별도로 저장하지 않음).
//
// Position 보정은 반드시 매니폴드 단위(1회)로 적용해야 한다.
// 접촉점별로 반복 적용하면 과보정이 발생한다.
struct Physics2DManifold
{
	EntityId A = INVALID_ENTITY_ID;
	EntityId B = INVALID_ENTITY_ID;

	Vector2<float> Normal      = Vector2<float>(0.0f, 0.0f);
	float          Penetration = 0.0f;

	Vector2<float> ContactPoints[2] = {};
	int            ContactCount     = 0;

	bool IsTrigger = false;
};
