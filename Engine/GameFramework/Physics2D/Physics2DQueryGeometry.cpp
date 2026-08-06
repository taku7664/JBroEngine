#include "pch.h"
#include "Physics2DQueryGeometry.h"

#include <cmath>
#include <limits>

namespace
{
	constexpr float QUERY_EPSILON    = 1e-6f;
	constexpr float QUERY_EPSILON_SQ = 1e-12f;

	float Cross(const Vector2& a, const Vector2& b)
	{
		return a.x * b.y - a.y * b.x;
	}

	float SignedArea(ArrayView<const Vector2> points)
	{
		float area = 0.0f;
		const std::size_t count = points.Size();
		for (std::size_t i = 0; i < count; ++i)
		{
			area += Cross(points[i], points[(i + 1) % count]);
		}
		return area * 0.5f;
	}

	// 다각형을 항상 CCW 순서로 읽게 해 주는 인덱스 어댑터.
	// 감기 방향이 CW 면 역순으로 훑는다 — 점 배열을 복사하지 않으므로 할당이 없다.
	// (바깥 법선을 구하려면 감기 방향이 확정돼야 하는데, ear clipping 이 낸 조각의
	//  감기 방향까지 호출자가 책임지게 하고 싶지 않다.)
	class ConvexWalk final
	{
	public:
		explicit ConvexWalk(ArrayView<const Vector2> points)
			: m_points(points)
			, m_reversed(SignedArea(points) < 0.0f)
		{
		}

		std::size_t Size() const { return m_points.Size(); }

		const Vector2& operator[](std::size_t index) const
		{
			return m_reversed ? m_points[m_points.Size() - 1 - index] : m_points[index];
		}

	private:
		ArrayView<const Vector2> m_points;
		bool                     m_reversed = false;
	};

	// CCW 다각형에서 엣지 p0→p1 의 바깥 방향 단위 법선. 길이 0 엣지면 false.
	bool OutwardNormal(const Vector2& p0, const Vector2& p1, Vector2& outNormal)
	{
		const Vector2 edge   = p1 - p0;
		const Vector2 normal = Vector2(edge.y, -edge.x);
		const float   lenSq  = normal.LengthSqrt();
		if (lenSq <= QUERY_EPSILON_SQ)
		{
			return false;
		}
		outNormal = normal * (1.0f / std::sqrt(lenSq));
		return true;
	}

	void ProjectOnAxis(const ConvexWalk& polygon, const Vector2& axis, float& outMin, float& outMax)
	{
		outMin = std::numeric_limits<float>::max();
		outMax = std::numeric_limits<float>::lowest();
		for (std::size_t i = 0; i < polygon.Size(); ++i)
		{
			const float projection = polygon[i].Dot(axis);
			outMin = projection < outMin ? projection : outMin;
			outMax = projection > outMax ? projection : outMax;
		}
	}

	Vector2 ClosestPointOnSegment(const Vector2& point, const Vector2& a, const Vector2& b)
	{
		const Vector2 ab    = b - a;
		const float   lenSq = ab.LengthSqrt();
		if (lenSq <= QUERY_EPSILON_SQ)
		{
			return a;
		}
		float t = (point - a).Dot(ab) / lenSq;
		t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
		return a + ab * t;
	}

	// 레이 vs 선분. 맞으면 레이 진행거리(outT)를 채운다.
	bool RayVsSegment(const Vector2& origin, const Vector2& direction, float maxDistance,
	                  const Vector2& a, const Vector2& b, float& outT)
	{
		const Vector2 edge  = b - a;
		const float   denom = Cross(direction, edge);
		if (std::abs(denom) <= QUERY_EPSILON)
		{
			return false;   // 평행 — 접하는 경우는 정점 원이 잡는다.
		}

		const Vector2 originToA = a - origin;
		const float   t         = Cross(originToA, edge) / denom;
		const float   u         = Cross(originToA, direction) / denom;
		if (t < 0.0f || t > maxDistance || u < 0.0f || u > 1.0f)
		{
			return false;
		}
		outT = t;
		return true;
	}
}

bool CPhysics2DQueryGeometry::RayVsCircle(const Vector2& origin, const Vector2& direction, float maxDistance,
                                          const Vector2& center, float radius,
                                          float& outT, Vector2& outNormal)
{
	const Vector2 originToCenter = origin - center;

	// 시작 시점에 이미 겹쳐 있으면 접촉 시각 0. (기존 Raycast 는 이 경우 반대편 exit
	//  지점을 돌려주지만, 스윕에서는 "이미 닿아 있다"가 맞는 답이다.)
	const float outsideDistanceSq = originToCenter.Dot(originToCenter) - radius * radius;
	if (outsideDistanceSq <= 0.0f)
	{
		outT      = 0.0f;
		outNormal = -direction;
		return true;
	}

	const float projection = originToCenter.Dot(direction);
	if (projection >= 0.0f)
	{
		return false;   // 원에서 멀어지는 방향.
	}

	const float discriminant = projection * projection - outsideDistanceSq;
	if (discriminant < 0.0f)
	{
		return false;
	}

	const float t = -projection - std::sqrt(discriminant);
	if (t < 0.0f || t > maxDistance)
	{
		return false;
	}

	const Vector2 hitPoint = origin + direction * t;
	Vector2       normal   = hitPoint - center;
	const float   lenSq    = normal.LengthSqrt();

	outT      = t;
	outNormal = lenSq > QUERY_EPSILON_SQ ? normal * (1.0f / std::sqrt(lenSq)) : -direction;
	return true;
}

bool CPhysics2DQueryGeometry::RayVsRoundedConvex(const Vector2& origin, const Vector2& direction, float maxDistance,
                                                 ArrayView<const Vector2> points, float radius,
                                                 float& outT, Vector2& outNormal)
{
	const std::size_t count = points.Size();
	if (0 == count)
	{
		return false;
	}
	if (1 == count)
	{
		return RayVsCircle(origin, direction, maxDistance, points[0], radius, outT, outNormal);
	}
	if (2 == count)
	{
		return false;   // 면적 0 콜라이더 — 솔버도 충돌시키지 않는다.
	}

	// 시작 시점 겹침 판정은 라운드 볼록 전체(모서리 둥근 부분 포함) 기준이어야 한다.
	// 엣지 평면을 radius 만큼 민 것만 보면 모서리 바깥쪽이 겹침으로 잘못 잡힌다.
	if (ConvexOverlapsCircle(points, origin, radius))
	{
		outT      = 0.0f;
		outNormal = -direction;
		return true;
	}

	const ConvexWalk polygon(points);

	float   bestT      = maxDistance;
	Vector2 bestNormal = -direction;
	bool    hit        = false;

	// ── 엣지를 바깥으로 radius 만큼 민 선분 ──────────────────────────────────
	for (std::size_t i = 0; i < count; ++i)
	{
		const Vector2& p0 = polygon[i];
		const Vector2& p1 = polygon[(i + 1) % count];

		Vector2 normal;
		if (false == OutwardNormal(p0, p1, normal))
		{
			continue;
		}
		if (direction.Dot(normal) >= 0.0f)
		{
			continue;   // 뒷면 — 볼록이므로 진입점이 될 수 없다.
		}

		const Vector2 offset = normal * radius;
		float         t      = 0.0f;
		if (false == RayVsSegment(origin, direction, bestT, p0 + offset, p1 + offset, t))
		{
			continue;
		}

		bestT      = t;
		bestNormal = normal;
		hit        = true;
	}

	// ── 모서리를 메우는 정점 원 ──────────────────────────────────────────────
	if (radius > 0.0f)
	{
		for (std::size_t i = 0; i < count; ++i)
		{
			float   t = 0.0f;
			Vector2 normal;
			if (false == RayVsCircle(origin, direction, bestT, polygon[i], radius, t, normal))
			{
				continue;
			}

			bestT      = t;
			bestNormal = normal;
			hit        = true;
		}
	}

	if (false == hit)
	{
		return false;
	}
	outT      = bestT;
	outNormal = bestNormal;
	return true;
}

bool CPhysics2DQueryGeometry::SweptConvexVsConvex(ArrayView<const Vector2> movingPoints,
                                                  ArrayView<const Vector2> staticPoints,
                                                  const Vector2& direction, float maxDistance,
                                                  float& outT, Vector2& outNormal)
{
	if (movingPoints.Size() < 3 || staticPoints.Size() < 3)
	{
		return false;
	}

	const ConvexWalk moving(movingPoints);
	const ConvexWalk stationary(staticPoints);

	// 각 분리축에서 두 투영 구간이 겹치는 시간 구간 [t0, t1] 을 구해 전부 교집합한다.
	// 교집합의 시작이 최초 접촉 시각이고, 그 시각을 만든 축이 접촉 법선이다.
	float   enterTime  = 0.0f;
	float   exitTime   = maxDistance;
	Vector2 bestNormal = -direction;
	bool    haveNormal = false;

	const std::size_t axisCount = moving.Size() + stationary.Size();
	for (std::size_t axisIndex = 0; axisIndex < axisCount; ++axisIndex)
	{
		Vector2 axis;
		if (axisIndex < moving.Size())
		{
			const std::size_t i = axisIndex;
			if (false == OutwardNormal(moving[i], moving[(i + 1) % moving.Size()], axis))
			{
				continue;
			}
		}
		else
		{
			const std::size_t i = axisIndex - moving.Size();
			if (false == OutwardNormal(stationary[i], stationary[(i + 1) % stationary.Size()], axis))
			{
				continue;
			}
		}

		float minMoving = 0.0f;
		float maxMoving = 0.0f;
		float minStatic = 0.0f;
		float maxStatic = 0.0f;
		ProjectOnAxis(moving, axis, minMoving, maxMoving);
		ProjectOnAxis(stationary, axis, minStatic, maxStatic);

		// 이동체가 이 축을 따라 단위 거리당 나아가는 양.
		const float speed = direction.Dot(axis);

		float t0 = 0.0f;
		float t1 = maxDistance;
		if (maxMoving < minStatic)
		{
			if (speed <= QUERY_EPSILON)
			{
				return false;   // 분리돼 있는데 다가가지 않는다.
			}
			t0 = (minStatic - maxMoving) / speed;
			t1 = (maxStatic - minMoving) / speed;
		}
		else if (maxStatic < minMoving)
		{
			if (speed >= -QUERY_EPSILON)
			{
				return false;
			}
			t0 = (maxStatic - minMoving) / speed;
			t1 = (minStatic - maxMoving) / speed;
		}
		else
		{
			// 이 축에서는 이미 겹쳐 있다 — 진입 제약은 없고 이탈 시각만 갱신한다.
			t0 = 0.0f;
			if (speed > QUERY_EPSILON)
			{
				t1 = (maxStatic - minMoving) / speed;
			}
			else if (speed < -QUERY_EPSILON)
			{
				t1 = (minStatic - maxMoving) / speed;
			}
		}

		if (t0 > enterTime)
		{
			enterTime = t0;
			// 이동체를 밀어내는 방향(= 정지체 표면의 바깥 법선)으로 정렬한다.
			bestNormal = speed < 0.0f ? axis : -axis;
			haveNormal = true;
		}
		if (t1 < exitTime)
		{
			exitTime = t1;
		}
		if (enterTime > exitTime)
		{
			return false;
		}
	}

	if (enterTime > maxDistance)
	{
		return false;
	}

	outT      = enterTime;
	outNormal = haveNormal ? bestNormal : -direction;
	return true;
}

bool CPhysics2DQueryGeometry::ConvexOverlapsConvex(ArrayView<const Vector2> a, ArrayView<const Vector2> b)
{
	if (a.Size() < 3 || b.Size() < 3)
	{
		return false;
	}

	const ConvexWalk polygonA(a);
	const ConvexWalk polygonB(b);

	const std::size_t axisCount = polygonA.Size() + polygonB.Size();
	for (std::size_t axisIndex = 0; axisIndex < axisCount; ++axisIndex)
	{
		Vector2 axis;
		if (axisIndex < polygonA.Size())
		{
			const std::size_t i = axisIndex;
			if (false == OutwardNormal(polygonA[i], polygonA[(i + 1) % polygonA.Size()], axis))
			{
				continue;
			}
		}
		else
		{
			const std::size_t i = axisIndex - polygonA.Size();
			if (false == OutwardNormal(polygonB[i], polygonB[(i + 1) % polygonB.Size()], axis))
			{
				continue;
			}
		}

		float minA = 0.0f;
		float maxA = 0.0f;
		float minB = 0.0f;
		float maxB = 0.0f;
		ProjectOnAxis(polygonA, axis, minA, maxA);
		ProjectOnAxis(polygonB, axis, minB, maxB);

		if (maxA < minB || maxB < minA)
		{
			return false;   // 분리축 발견.
		}
	}
	return true;
}

bool CPhysics2DQueryGeometry::ConvexOverlapsCircle(ArrayView<const Vector2> points, const Vector2& center, float radius)
{
	const std::size_t count = points.Size();
	if (0 == count)
	{
		return false;
	}

	const float radiusSq = radius * radius;
	if (1 == count)
	{
		return (center - points[0]).LengthSqrt() <= radiusSq;
	}

	const ConvexWalk polygon(points);

	// 중심이 안쪽인지(볼록: 모든 바깥 법선에 대해 부호거리 <= 0)와
	// 가장 가까운 엣지까지의 거리를 한 번의 순회로 같이 구한다.
	bool  centerInside     = count >= 3;
	float nearestDistanceSq = std::numeric_limits<float>::max();

	for (std::size_t i = 0; i < count; ++i)
	{
		const Vector2& p0 = polygon[i];
		const Vector2& p1 = polygon[(i + 1) % count];

		const Vector2 closest    = ClosestPointOnSegment(center, p0, p1);
		const float   distanceSq = (center - closest).LengthSqrt();
		nearestDistanceSq = distanceSq < nearestDistanceSq ? distanceSq : nearestDistanceSq;

		Vector2 normal;
		if (centerInside && OutwardNormal(p0, p1, normal) && (center - p0).Dot(normal) > 0.0f)
		{
			centerInside = false;
		}
	}

	return centerInside || nearestDistanceSq <= radiusSq;
}

void CPhysics2DQueryGeometry::BuildBoxPoints(const Vector2& center, const Vector2& halfExtents, float rotationRadians,
                                             Vector2 outPoints[4])
{
	const float cosine = std::cos(rotationRadians);
	const float sine   = std::sin(rotationRadians);

	const float halfWidth  = std::abs(halfExtents.x);
	const float halfHeight = std::abs(halfExtents.y);

	// CCW 순서(우하 → 우상 → 좌상 → 좌하 기준으로 회전 적용).
	const Vector2 localCorners[4] = {
		Vector2(-halfWidth, -halfHeight),
		Vector2( halfWidth, -halfHeight),
		Vector2( halfWidth,  halfHeight),
		Vector2(-halfWidth,  halfHeight),
	};

	for (int i = 0; i < 4; ++i)
	{
		const Vector2& local = localCorners[i];
		outPoints[i] = center + Vector2(local.x * cosine - local.y * sine,
		                                local.x * sine   + local.y * cosine);
	}
}
