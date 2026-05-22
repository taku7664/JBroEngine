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

struct Physics2DContact
{
	EntityId A = INVALID_ENTITY_ID;
	EntityId B = INVALID_ENTITY_ID;
	Vector2<float> Normal = Vector2<float>(0.0f, 0.0f);
	float Penetration = 0.0f;
	bool IsTrigger = false;
};
