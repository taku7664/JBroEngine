#pragma once

#include "GameFramework/Physics2D/Physics2DTypes.h"
#include "Utillity/Vector2T.h"

#include <cmath>
#include <cstdint>
#include <vector>

struct Rigidbody2D
{
	bool IsEnabled = true;
	EPhysics2DBodyType BodyType = EPhysics2DBodyType::Dynamic;
	Vector2<float> Velocity = Vector2<float>(0.0f, 0.0f);
	Vector2<float> Force = Vector2<float>(0.0f, 0.0f);
	float AngularVelocity = 0.0f;
	float Torque = 0.0f;
	float Mass = 1.0f;
	float InverseMass = 1.0f;
	float Inertia = 1.0f;
	float InverseInertia = 1.0f;
	float Friction = 0.3f;          // 캐릭터 기본: 중간 마찰
	float Restitution = 0.0f;       // 캐릭터 기본: 반발 없음
	float LinearDamping = 0.0f;     // 캐릭터는 스크립트로 속도 제어
	float AngularDamping = 5.0f;    // 강한 각감쇠 (회전 빠르게 멈춤)
	float GravityScale = 2.0f;      // 플랫포머 표준: 빠른 낙하감
	bool UseGravity = true;
	bool FreezePositionX = false;
	bool FreezePositionY = false;
	bool FreezeRotation = true;     // 캐릭터 기본: 회전 고정
	bool StabilizeRestingContacts = true;
	float RestingLinearVelocityThreshold = 0.05f;
	float RestingAngularVelocityThreshold = 0.1f;
	std::uint32_t LastContactCount = 0;
	Vector2<float> LastContactNormal = Vector2<float>(0.0f, 0.0f);
	Vector2<float> LastContactPoint = Vector2<float>(0.0f, 0.0f);
	float LastNormalImpulse = 0.0f;
	float LastFrictionImpulse = 0.0f;
	float LastAngularImpulse = 0.0f;

	void SetMass(float mass)
	{
		Mass = mass > 0.0f ? mass : 0.0f;
		InverseMass = Mass > 0.0f ? 1.0f / Mass : 0.0f;
	}

	void SetInertia(float inertia)
	{
		Inertia = inertia > 0.0f ? inertia : 0.0f;
		InverseInertia = Inertia > 0.0f ? 1.0f / Inertia : 0.0f;
	}
};

struct PolygonCollider2D
{
	bool IsEnabled = true;
	Vector2<float> LocalCenter = Vector2<float>(0.0f, 0.0f);
	std::uint32_t VertexCount = 4;
	Vector2<float> Size = Vector2<float>(1.0f, 1.0f);
	float RotationRadians = 0.0f;
	std::vector<Vector2<float>> LocalPoints;
	std::vector<Vector2<float>> WorldPoints;
	PhysicsAABB2D WorldAABB;
	bool IsTrigger = false;
	bool IsConvex = true;

	void BuildLocalPoints(std::vector<Vector2<float>>& outPoints) const
	{
		const std::uint32_t N = VertexCount < 3 ? 3 : VertexCount;
		const float hw = Size.x > 0.0f ? Size.x * 0.5f : 0.0f;
		const float hh = Size.y > 0.0f ? Size.y * 0.5f : 0.0f;
		constexpr float PI = 3.14159265358979323846f;

		outPoints.clear();
		outPoints.reserve(N);

		const float cr = std::cos(RotationRadians);
		const float sr = std::sin(RotationRadians);

		auto rotatePoint = [cr, sr](float x, float y) -> Vector2<float>
		{
			return Vector2<float>(x * cr - y * sr, x * sr + y * cr);
		};

		if (N == 3)
		{
			// Flat-bottom triangle: top vertex at (0, hh), base at y = -hh spanning full width.
			const Vector2<float> pts[3] = {
				{  0.0f,  hh },
				{ -hw,   -hh },
				{  hw,   -hh }
			};
			for (const auto& p : pts)
			{
				const Vector2<float> r = rotatePoint(p.x, p.y);
				outPoints.emplace_back(LocalCenter.x + r.x, LocalCenter.y + r.y);
			}
		}
		else if (N == 4)
		{
			// Axis-aligned rectangle: four corners fill the bounding box exactly.
			const Vector2<float> pts[4] = {
				{ -hw,  hh },
				{  hw,  hh },
				{  hw, -hh },
				{ -hw, -hh }
			};
			for (const auto& p : pts)
			{
				const Vector2<float> r = rotatePoint(p.x, p.y);
				outPoints.emplace_back(LocalCenter.x + r.x, LocalCenter.y + r.y);
			}
		}
		else
		{
			// N >= 5: regular polygon inscribed in the bounding ellipse.
			// Start at top (PI/2). Even-N uses half-step offset for flat top & bottom.
			const float startAngle = (N % 2 == 0)
				? (PI / 2.0f + PI / static_cast<float>(N))
				: (PI / 2.0f);

			for (std::uint32_t i = 0; i < N; ++i)
			{
				const float theta = startAngle + (2.0f * PI * static_cast<float>(i)) / static_cast<float>(N);
				const float lx = std::cos(theta) * hw;
				const float ly = std::sin(theta) * hh;
				const Vector2<float> r = rotatePoint(lx, ly);
				outPoints.emplace_back(LocalCenter.x + r.x, LocalCenter.y + r.y);
			}
		}
	}

	void BuildLocalBoundsPoints(std::vector<Vector2<float>>& outPoints) const
	{
		const float halfWidth = Size.x > 0.0f ? Size.x * 0.5f : 0.0f;
		const float halfHeight = Size.y > 0.0f ? Size.y * 0.5f : 0.0f;
		const float c = std::cos(RotationRadians);
		const float s = std::sin(RotationRadians);

		const Vector2<float> corners[4] = {
			Vector2<float>(-halfWidth, -halfHeight),
			Vector2<float>(halfWidth, -halfHeight),
			Vector2<float>(halfWidth, halfHeight),
			Vector2<float>(-halfWidth, halfHeight)
		};

		outPoints.clear();
		outPoints.reserve(4);
		for (const Vector2<float>& corner : corners)
		{
			outPoints.emplace_back(
				LocalCenter.x + corner.x * c - corner.y * s,
				LocalCenter.y + corner.x * s + corner.y * c);
		}
	}
};

struct CircleCollider2D
{
	bool IsEnabled = true;
	Vector2<float> LocalCenter = Vector2<float>(0.0f, 0.0f);
	Vector2<float> WorldCenter = Vector2<float>(0.0f, 0.0f);
	float Radius = 0.5f;
	float WorldRadius = 0.5f;
	PhysicsAABB2D WorldAABB;
	bool IsTrigger = false;
};
