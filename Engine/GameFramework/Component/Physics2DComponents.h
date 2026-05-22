#pragma once

#include "GameFramework/Physics2D/Physics2DTypes.h"
#include "Utillity/Vector2T.h"

#include <vector>

struct Rigidbody2D
{
	EPhysics2DBodyType BodyType = EPhysics2DBodyType::Dynamic;
	Vector2<float> Velocity = Vector2<float>(0.0f, 0.0f);
	Vector2<float> Force = Vector2<float>(0.0f, 0.0f);
	float Mass = 1.0f;
	float InverseMass = 1.0f;
	float Friction = 0.5f;
	float Restitution = 0.0f;
	float LinearDamping = 0.0f;
	float GravityScale = 1.0f;
	bool UseGravity = true;
	bool FreezePositionX = false;
	bool FreezePositionY = false;

	void SetMass(float mass)
	{
		Mass = mass > 0.0f ? mass : 0.0f;
		InverseMass = Mass > 0.0f ? 1.0f / Mass : 0.0f;
	}
};

struct PolygonCollider2D
{
	std::vector<Vector2<float>> LocalPoints;
	std::vector<Vector2<float>> WorldPoints;
	PhysicsAABB2D WorldAABB;
	bool IsTrigger = false;
	bool IsConvex = true;
};

struct CircleCollider2D
{
	Vector2<float> LocalCenter = Vector2<float>(0.0f, 0.0f);
	Vector2<float> WorldCenter = Vector2<float>(0.0f, 0.0f);
	float Radius = 0.5f;
	float WorldRadius = 0.5f;
	PhysicsAABB2D WorldAABB;
	bool IsTrigger = false;
};
