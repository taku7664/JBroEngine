#pragma once

#include "GameFramework/Physics2D/Physics2DTypes.h"
#include "Utillity/Math/Vector2T.h"

#include <cmath>
#include <cstdint>
#include <vector>

struct Rigidbody2D
{
	bool IsEnabled = true;
	EPhysics2DBodyType BodyType = EPhysics2DBodyType::Dynamic;
	Vector2 Velocity = Vector2(0.0f, 0.0f);
	Vector2 Force = Vector2(0.0f, 0.0f);
	float AngularVelocity = 0.0f;
	float Torque = 0.0f;
	float Mass = 1.0f;
	float InverseMass = 1.0f;
	float Inertia = 1.0f;
	float InverseInertia = 1.0f;
	float Friction = 0.5f;          // 기본: 일반 바닥 수준 마찰
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
	Vector2 LastContactNormal = Vector2(0.0f, 0.0f);
	Vector2 LastContactPoint = Vector2(0.0f, 0.0f);
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

// 오목 폴리곤 분해 시 생성되는 볼록 조각(삼각형) 하나.
// Physics2DSystem 이 Ear Clipping 으로 생성하며, SAT 는 이 단위로 수행된다.
struct ConvexPiece2D
{
	std::vector<Vector2> LocalPoints;   // 삼각형 3 꼭짓점 (로컬 공간)
	std::vector<Vector2> WorldPoints;   // 삼각형 3 꼭짓점 (월드 공간)
	std::vector<bool> BoundaryEdges;           // 원본 폴리곤 외곽 엣지이면 true, 삼각분해 내부 대각선이면 false
	PhysicsAABB2D               WorldBounds;   // 브로드 페이즈용 AABB
};

struct PolygonCollider2D
{
	bool IsEnabled = true;
	std::uint32_t VertexCount = 4;
	// LocalPoints: 커스텀 버텍스 편집 또는 Physics2DSystem 이 빌드한 포인트.
	// Physics2DSystem 은 절차적 파라미터(VertexCount)가 변경된 경우에만 재빌드한다(dirty 캐시).
	// 에디터가 LocalPoints 를 직접 수정하면 다음 파라미터 변경 전까지 해당 포인트가 그대로 유지된다.
	std::vector<Vector2> LocalPoints;
	std::vector<Vector2> WorldPoints;
	PhysicsAABB2D WorldAABB;
	bool IsTrigger = false;

	// ── 볼록 분해 조각 (오목 폴리곤 충돌용) ──────────────────────────────────
	// Physics2DSystem 이 LocalPoints 변경 시 Ear Clipping 으로 재생성.
	// SAT 는 ConvexPieces 단위로 수행된다.
	std::vector<ConvexPiece2D> ConvexPieces;
	// LocalPoints 가 바뀔 때마다 true → UpdateColliderBounds 에서 재분해 후 false.
	bool m_convexDirty = true;
	std::uint64_t m_builtLocalPointsHash = 0;

	// ── 절차적 재빌드 dirty 캐시 ─────────────────────────────────────────────
	// Physics2DSystem 내부에서만 갱신. 초기값은 실제 필드 기본값과 다르게 설정해
	// 첫 번째 Update 에서 반드시 빌드되도록 한다.
	std::uint32_t m_builtVertexCount = 0;

	// 절차적 파라미터가 마지막 빌드 이후 변경됐으면 true.
	bool NeedsProceduralRebuild() const
	{
		return LocalPoints.empty()
			|| VertexCount != m_builtVertexCount;
	}

	// 빌드 완료 후 호출 — 다음 NeedsProceduralRebuild 가 false 를 반환하도록 갱신.
	void MarkProceduralBuilt()
	{
		m_builtVertexCount = VertexCount;
	}

	// BuildLocalPoints: 절차적 생성 결과를 outPoints 에 기록한다.
	// 크기는 항상 단위 크기(반지름 0.5)를 사용하며, 실제 크기는 Transform2D Scale 로 제어한다.
	// Physics2DSystem 은 NeedsProceduralRebuild() 가 true 일 때만 이 함수를 호출한다.
	void BuildLocalPoints(std::vector<Vector2>& outPoints) const
	{
		const std::uint32_t N = VertexCount < 3 ? 3 : VertexCount;
		constexpr float hw = 0.5f;
		constexpr float hh = 0.5f;
		constexpr float PI = 3.14159265358979323846f;

		outPoints.clear();
		outPoints.reserve(N);

		if (N == 3)
		{
			outPoints.push_back({  0.0f,  hh });
			outPoints.push_back({ -hw,   -hh });
			outPoints.push_back({  hw,   -hh });
		}
		else if (N == 4)
		{
			outPoints.push_back({ -hw,  hh });
			outPoints.push_back({  hw,  hh });
			outPoints.push_back({  hw, -hh });
			outPoints.push_back({ -hw, -hh });
		}
		else
		{
			const float startAngle = (N % 2 == 0)
				? (PI / 2.0f + PI / static_cast<float>(N))
				: (PI / 2.0f);

			for (std::uint32_t i = 0; i < N; ++i)
			{
				const float theta = startAngle + (2.0f * PI * static_cast<float>(i)) / static_cast<float>(N);
				outPoints.push_back({ std::cos(theta) * hw, std::sin(theta) * hh });
			}
		}
	}
};

struct CircleCollider2D
{
	bool IsEnabled = true;
	Vector2 WorldCenter = Vector2(0.0f, 0.0f);
	float Radius = 0.5f;
	float WorldRadius = 0.5f;
	PhysicsAABB2D WorldAABB;
	bool IsTrigger = false;
};
