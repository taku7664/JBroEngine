#include "pch.h"
#include "Physics2DSystem.h"

#include "Core/Core.h"
#include "Core/Time/Time.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Physics2D/Physics2DTypes.h"
#include "GameFramework/Scene/Scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
	// ── Solver 상수 ───────────────────────────────────────────────────────────────
	//
	// Box2D / Godot / PhysX 공통 설계 원칙:
	//   velocity solver 와 position solver 는 완전히 분리된다.
	//   velocity solver 는 순수하게 속도 impulse 만 처리한다.
	//   위치 오차(침투) 교정은 오직 position solver 에서만 position 을 직접 이동시킨다.
	//   두 solver 를 섞으면 "교정 속도가 실제 velocity 에 누적되어 영구 drift" 가 발생한다.
	//
	// POSITION_CORRECTION_PERCENT (Box2D 의 b2_baumgarte 에 해당):
	//   한 iteration 에서 침투량의 몇 %를 위치 직접 보정할지.
	//
	//   ★ 안전 조건: positionIterations × PERCENT < 1.0
	//     iterations 간 manifold.Penetration 이 갱신되지 않으므로
	//     총 보정량 = iterations × PERCENT × (d - slop).
	//     이 값이 침투량 d 를 초과하면 표면을 통과해 매 프레임 진동(떨림)이 발생한다.
	//     positionIterations=2 기준 → PERCENT < 0.5 필수.
	//     0.4 선택: 2 × 0.4 = 0.8 < 1.0 ✓,  sub-step=2 결합 시 스텝당 수렴률 ≈ 84%.
	//
	// POSITION_CORRECTION_SLOP:
	//   이 이하의 침투는 무시.  미세한 수치 오차로 인한 보정 진동 방지.
	//
	// RESTITUTION_VELOCITY_THRESHOLD:
	//   충돌 속도가 이 이하면 반발계수를 0으로 취급.
	//   정지 물체가 미세하게 튀는 현상 방지.
	//
	constexpr float MIN_PHYSICS_DELTA_SECONDS      = 0.0001f;
	constexpr float POSITION_CORRECTION_PERCENT    = 0.6f;   // 이전 0.4 → 침투 수렴 가속
	constexpr float POSITION_CORRECTION_SLOP       = 0.005f; // 이전 0.01 → 얕은 침투도 보정
	constexpr float RESTITUTION_VELOCITY_THRESHOLD = 1.0f;
	constexpr float MIN_NORMAL_LENGTH_SQ           = 0.000001f;

	// ── 수학 유틸 ─────────────────────────────────────────────────────────────────

	float Dot(const Vector2<float>& a, const Vector2<float>& b)
	{
		return a.x * b.x + a.y * b.y;
	}

	float Cross(const Vector2<float>& a, const Vector2<float>& b)
	{
		return a.x * b.y - a.y * b.x;
	}

	Vector2<float> Cross(float scalar, const Vector2<float>& v)
	{
		return Vector2<float>(-scalar * v.y, scalar * v.x);
	}

	// 영벡터에서 NaN을 반환하지 않는 안전한 정규화.
	Vector2<float> NormalizeSafe(const Vector2<float>& v,
	                             const Vector2<float>& fallback = Vector2<float>(1.0f, 0.0f))
	{
		const float lenSq = v.LengthSqrt();
		if (lenSq <= MIN_NORMAL_LENGTH_SQ)
		{
			return fallback;
		}
		return v * (1.0f / std::sqrt(lenSq));
	}

	// 법선을 A→B 방향으로 정렬한다.
	// 실제 충돌한 콜라이더 중심을 직접 전달해 GetColliderCenter 우회 (8순위 수정).
	void OrientNormal(Vector2<float>& normal,
	                  const Vector2<float>& centerA,
	                  const Vector2<float>& centerB)
	{
		if (Dot(centerB - centerA, normal) < 0.0f)
		{
			normal = -normal;
		}
	}

	bool IntersectsAABB(const PhysicsAABB2D& a, const PhysicsAABB2D& b)
	{
		return a.Min.x <= b.Max.x && a.Max.x >= b.Min.x
		    && a.Min.y <= b.Max.y && a.Max.y >= b.Min.y;
	}

	PhysicsAABB2D CalculateAABB(const std::vector<Vector2<float>>& points)
	{
		PhysicsAABB2D aabb;
		if (points.empty())
		{
			return aabb;
		}

		aabb.Min = points[0];
		aabb.Max = points[0];
		for (const Vector2<float>& point : points)
		{
			aabb.Min.x = std::min(aabb.Min.x, point.x);
			aabb.Min.y = std::min(aabb.Min.y, point.y);
			aabb.Max.x = std::max(aabb.Max.x, point.x);
			aabb.Max.y = std::max(aabb.Max.y, point.y);
		}
		return aabb;
	}

	Vector2<float> CalculateCenter(const std::vector<Vector2<float>>& points)
	{
		Vector2<float> center(0.0f, 0.0f);
		if (points.empty())
		{
			return center;
		}

		for (const Vector2<float>& point : points)
		{
			center += point;
		}

		return center / static_cast<float>(points.size());
	}

	float CalculateSignedArea(const std::vector<Vector2<float>>& points)
	{
		float area = 0.0f;
		const std::size_t count = points.size();
		for (std::size_t i = 0; i < count; ++i)
		{
			const Vector2<float>& a = points[i];
			const Vector2<float>& b = points[(i + 1) % count];
			area += a.x * b.y - b.x * a.y;
		}
		return area * 0.5f;
	}

	bool IsConvexPolygon(const std::vector<Vector2<float>>& points)
	{
		const std::size_t count = points.size();
		if (count < 4)
		{
			return true;
		}

		float sign = 0.0f;
		for (std::size_t i = 0; i < count; ++i)
		{
			const Vector2<float>& a = points[i];
			const Vector2<float>& b = points[(i + 1) % count];
			const Vector2<float>& c = points[(i + 2) % count];
			const float cross = Cross(b - a, c - b);
			if (std::abs(cross) <= MIN_NORMAL_LENGTH_SQ)
			{
				continue;
			}
			const float currentSign = cross > 0.0f ? 1.0f : -1.0f;
			if (0.0f == sign)
			{
				sign = currentSign;
			}
			else if (sign != currentSign)
			{
				return false;
			}
		}
		return true;
	}

	bool PointInPolygon(const Vector2<float>& point, const std::vector<Vector2<float>>& polygon)
	{
		bool inside = false;
		const std::size_t count = polygon.size();
		for (std::size_t i = 0, j = count - 1; i < count; j = i++)
		{
			const Vector2<float>& a = polygon[i];
			const Vector2<float>& b = polygon[j];
			const bool crosses = ((a.y > point.y) != (b.y > point.y))
				&& (point.x < (b.x - a.x) * (point.y - a.y) / ((b.y - a.y) + 0.0000001f) + a.x);
			if (crosses)
			{
				inside = !inside;
			}
		}
		return inside;
	}

	Vector2<float> GetPolygonOutwardNormal(const std::vector<Vector2<float>>& polygon, std::size_t edgeIndex)
	{
		const Vector2<float>& a = polygon[edgeIndex];
		const Vector2<float>& b = polygon[(edgeIndex + 1) % polygon.size()];
		const Vector2<float> edge = b - a;
		const bool isCcw = CalculateSignedArea(polygon) >= 0.0f;
		const Vector2<float> outward = isCcw
			? Vector2<float>(edge.y, -edge.x)
			: Vector2<float>(-edge.y, edge.x);
		return NormalizeSafe(outward);
	}

	Matrix3x2 CalculateWorldTransformNow(const CScene& scene, EntityId entity)
	{
		const Transform2D* transform = scene.GetComponent<Transform2D>(entity);
		Matrix3x2 worldTransform = transform ? transform->ToMatrix3x2() : Matrix3x2::Identity();

		EntityId parent = scene.GetParent(entity);
		while (INVALID_ENTITY_ID != parent)
		{
			const Transform2D* parentTransform = scene.GetComponent<Transform2D>(parent);
			if (parentTransform)
			{
				worldTransform = worldTransform * parentTransform->ToMatrix3x2();
			}
			parent = scene.GetParent(parent);
		}

		return worldTransform;
	}

	Matrix3x2 CalculateParentWorldTransformNow(const CScene& scene, EntityId entity)
	{
		const EntityId parent = scene.GetParent(entity);
		return parent != INVALID_ENTITY_ID
			? CalculateWorldTransformNow(scene, parent)
			: Matrix3x2::Identity();
	}

	Vector2<float> TransformVector(const Matrix3x2& matrix, const Vector2<float>& vector)
	{
		return Vector2<float>(
			vector.x * matrix.M11 + vector.y * matrix.M21,
			vector.x * matrix.M12 + vector.y * matrix.M22
		);
	}

	Vector2<float> WorldVectorToParentLocal(const CScene& scene, EntityId entity, const Vector2<float>& worldVector)
	{
		Matrix3x2 parentWorld = CalculateParentWorldTransformNow(scene, entity);
		Matrix3x2 parentWorldInverse;
		if (false == parentWorld.TryInvert(parentWorldInverse))
		{
			return worldVector;
		}
		return TransformVector(parentWorldInverse, worldVector);
	}

	std::uint64_t HashLocalPoints(const std::vector<Vector2<float>>& points)
	{
		constexpr std::uint64_t FNV_OFFSET = 14695981039346656037ull;
		constexpr std::uint64_t FNV_PRIME  = 1099511628211ull;

		std::uint64_t hash = FNV_OFFSET;
		auto Mix = [&hash](std::uint32_t bits)
		{
			hash ^= static_cast<std::uint64_t>(bits);
			hash *= FNV_PRIME;
		};

		Mix(static_cast<std::uint32_t>(points.size()));
		for (const Vector2<float>& point : points)
		{
			std::uint32_t xBits = 0;
			std::uint32_t yBits = 0;
			std::memcpy(&xBits, &point.x, sizeof(float));
			std::memcpy(&yBits, &point.y, sizeof(float));
			Mix(xBits);
			Mix(yBits);
		}
		return hash;
	}

	Vector2<float> SupportPoint(const std::vector<Vector2<float>>& points, const Vector2<float>& dir)
	{
		float maxDot = -std::numeric_limits<float>::max();
		Vector2<float> best = points[0];
		for (const Vector2<float>& p : points)
		{
			const float d = Dot(p, dir);
			if (d > maxDot)
			{
				maxDot = d;
				best   = p;
			}
		}
		return best;
	}

	void ProjectPolygon(const std::vector<Vector2<float>>& points, const Vector2<float>& axis,
	                    float& outMin, float& outMax)
	{
		outMin = Dot(points[0], axis);
		outMax = outMin;
		for (const Vector2<float>& point : points)
		{
			const float proj = Dot(point, axis);
			outMin = std::min(outMin, proj);
			outMax = std::max(outMax, proj);
		}
	}

	bool IsBoundaryAxis(const std::vector<bool>* boundaryEdges, std::size_t edgeIndex)
	{
		return nullptr == boundaryEdges
			|| edgeIndex >= boundaryEdges->size()
			|| (*boundaryEdges)[edgeIndex];
	}

	bool TestPolygonSAT(const std::vector<Vector2<float>>& a, const std::vector<Vector2<float>>& b,
	                    const std::vector<bool>* aBoundaryEdges, const std::vector<bool>* bBoundaryEdges,
	                    Vector2<float>& outNormal, float& outPenetration)
	{
		// 모든 엣지(대각선 포함)로 분리 축 검사 — 겹침 없으면 즉시 false.
		// 접촉 법선/침투값은 경계(Boundary) 엣지 중 최소 침투 축에서만 선택.
		// 이렇게 해야 내부 대각선이 법선 후보로 잘못 선택되는 것을 막는다.
		float              boundaryMinPen     = std::numeric_limits<float>::max();
		Vector2<float>     boundaryBestNormal = {};
		bool               hasBoundaryAxis    = false;

		for (int shape = 0; shape < 2; ++shape)
		{
			const std::vector<Vector2<float>>& points        = (shape == 0) ? a : b;
			const std::vector<bool>*           boundaryEdges = (shape == 0) ? aBoundaryEdges : bBoundaryEdges;

			for (std::size_t i = 0; i < points.size(); ++i)
			{
				const Vector2<float>& p0        = points[i];
				const Vector2<float>& p1        = points[(i + 1) % points.size()];
				const Vector2<float>  edge      = p1 - p0;
				const Vector2<float>  perpRaw(-edge.y, edge.x);
				const float           perpLenSq = perpRaw.LengthSqrt();
				if (perpLenSq <= MIN_NORMAL_LENGTH_SQ)
				{
					continue; // 길이 0 엣지 — 법선 계산 불가, 건너뜀
				}
				const Vector2<float> axis = perpRaw * (1.0f / std::sqrt(perpLenSq));

				float minA, maxA, minB, maxB;
				ProjectPolygon(a, axis, minA, maxA);
				ProjectPolygon(b, axis, minB, maxB);
				const float overlap = std::min(maxA, maxB) - std::max(minA, minB);
				if (overlap <= 0.0f)
				{
					return false; // 분리 축 발견 — 충돌 없음
				}

				// 경계 엣지일 때만 법선/침투 후보로 등록
				if (IsBoundaryAxis(boundaryEdges, i) && overlap < boundaryMinPen)
				{
					boundaryMinPen     = overlap;
					boundaryBestNormal = axis;
					hasBoundaryAxis    = true;
				}
			}
		}

		if (!hasBoundaryAxis)
		{
			return false; // 경계 엣지가 전혀 없음 (모두 대각선)
		}

		outNormal      = boundaryBestNormal;
		outPenetration = boundaryMinPen;
		return true;
	}

	bool TestPolygonSAT(const std::vector<Vector2<float>>& a, const std::vector<Vector2<float>>& b,
	                    Vector2<float>& outNormal, float& outPenetration)
	{
		return TestPolygonSAT(a, b, nullptr, nullptr, outNormal, outPenetration);
	}

	Vector2<float> ClosestPointOnSegment(const Vector2<float>& point,
	                                     const Vector2<float>& a, const Vector2<float>& b)
	{
		const Vector2<float> ab       = b - a;
		const float          lengthSq = ab.LengthSqrt();
		if (lengthSq <= MIN_NORMAL_LENGTH_SQ)
		{
			return a;
		}
		const float t = std::clamp(Dot(point - a, ab) / lengthSq, 0.0f, 1.0f);
		return a + ab * t;
	}

	struct BoundaryHit2D
	{
		bool Hit = false;
		Vector2<float> Normal = Vector2<float>(1.0f, 0.0f);
		Vector2<float> Point = Vector2<float>(0.0f, 0.0f);
		float Penetration = 0.0f;
	};

	BoundaryHit2D FindClosestBoundaryHit(const std::vector<Vector2<float>>& polygon,
	                                     const Vector2<float>& point,
	                                     bool pointInside)
	{
		BoundaryHit2D result;
		if (polygon.size() < 2)
		{
			return result;
		}

		float bestDistanceSq = std::numeric_limits<float>::max();
		std::size_t bestEdge = 0;
		Vector2<float> bestPoint = polygon[0];
		for (std::size_t i = 0; i < polygon.size(); ++i)
		{
			const Vector2<float>& a = polygon[i];
			const Vector2<float>& b = polygon[(i + 1) % polygon.size()];
			const Vector2<float> closest = ClosestPointOnSegment(point, a, b);
			const float distanceSq = (point - closest).LengthSqrt();
			if (distanceSq < bestDistanceSq)
			{
				bestDistanceSq = distanceSq;
				bestEdge = i;
				bestPoint = closest;
			}
		}

		const float distance = std::sqrt(bestDistanceSq);
		Vector2<float> normal = pointInside
			? GetPolygonOutwardNormal(polygon, bestEdge)
			: NormalizeSafe(point - bestPoint, GetPolygonOutwardNormal(polygon, bestEdge));

		const Vector2<float> outward = GetPolygonOutwardNormal(polygon, bestEdge);
		if (Dot(normal, outward) < 0.0f)
		{
			normal = -normal;
		}

		result.Hit = true;
		result.Normal = normal;
		result.Point = bestPoint;
		result.Penetration = distance;
		return result;
	}

	bool TrySegmentIntersection(const Vector2<float>& a0, const Vector2<float>& a1,
	                            const Vector2<float>& b0, const Vector2<float>& b1,
	                            Vector2<float>& outPoint)
	{
		const Vector2<float> r = a1 - a0;
		const Vector2<float> s = b1 - b0;
		const float denom = Cross(r, s);
		if (std::abs(denom) <= MIN_NORMAL_LENGTH_SQ)
		{
			return false;
		}

		const Vector2<float> diff = b0 - a0;
		const float t = Cross(diff, s) / denom;
		const float u = Cross(diff, r) / denom;
		if (t < 0.0f || t > 1.0f || u < 0.0f || u > 1.0f)
		{
			return false;
		}

		outPoint = a0 + r * t;
		return true;
	}

	bool TestBoundaryPolygonCircle(const PolygonCollider2D& polygon, const CircleCollider2D& circle,
	                               Vector2<float>& outNormal, Vector2<float>& outContactPoint,
	                               float& outPenetration)
	{
		if (polygon.WorldPoints.size() < 3 || circle.WorldRadius <= 0.0f)
		{
			return false;
		}

		const bool centerInside = PointInPolygon(circle.WorldCenter, polygon.WorldPoints);
		BoundaryHit2D hit = FindClosestBoundaryHit(polygon.WorldPoints, circle.WorldCenter, centerInside);
		if (!hit.Hit)
		{
			return false;
		}

		const float distance = hit.Penetration;
		if (!centerInside && distance > circle.WorldRadius)
		{
			return false;
		}

		outNormal = hit.Normal;
		outPenetration = centerInside
			? circle.WorldRadius + distance
			: circle.WorldRadius - distance;
		outContactPoint = circle.WorldCenter - outNormal * circle.WorldRadius;
		return outPenetration > 0.0f;
	}

	bool TestBoundaryPolygonPolygonOneWay(const std::vector<Vector2<float>>& boundaryPolygon,
	                                      const std::vector<Vector2<float>>& testPolygon,
	                                      Vector2<float>& outNormal,
	                                      Vector2<float>& outContactPoint,
	                                      float& outPenetration)
	{
		bool hitAny = false;
		float bestPenetration = -std::numeric_limits<float>::max();
		Vector2<float> bestNormal(1.0f, 0.0f);
		Vector2<float> bestPoint(0.0f, 0.0f);

		for (const Vector2<float>& point : testPolygon)
		{
			if (!PointInPolygon(point, boundaryPolygon))
			{
				continue;
			}

			BoundaryHit2D hit = FindClosestBoundaryHit(boundaryPolygon, point, true);
			if (!hit.Hit)
			{
				continue;
			}

			if (hit.Penetration > bestPenetration)
			{
				bestPenetration = hit.Penetration;
				bestNormal = hit.Normal;
				bestPoint = point;
				hitAny = true;
			}
		}

		if (!hitAny)
		{
			for (std::size_t bi = 0; bi < boundaryPolygon.size(); ++bi)
			{
				const Vector2<float>& ba = boundaryPolygon[bi];
				const Vector2<float>& bb = boundaryPolygon[(bi + 1) % boundaryPolygon.size()];
				for (std::size_t ti = 0; ti < testPolygon.size(); ++ti)
				{
					const Vector2<float>& ta = testPolygon[ti];
					const Vector2<float>& tb = testPolygon[(ti + 1) % testPolygon.size()];
					Vector2<float> intersection;
					if (!TrySegmentIntersection(ba, bb, ta, tb, intersection))
					{
						continue;
					}

					outNormal = GetPolygonOutwardNormal(boundaryPolygon, bi);
					outContactPoint = intersection;
					outPenetration = POSITION_CORRECTION_SLOP * 2.0f;
					return true;
				}
			}
		}

		if (!hitAny)
		{
			return false;
		}

		outNormal = bestNormal;
		outContactPoint = bestPoint;
		outPenetration = bestPenetration;
		return outPenetration > 0.0f;
	}

	bool TestBoundaryPolygonPolygon(const PolygonCollider2D& a, const PolygonCollider2D& b,
	                                Vector2<float>& outNormal,
	                                Vector2<float>& outContactPoint,
	                                float& outPenetration)
	{
		Vector2<float> nAB;
		Vector2<float> pAB;
		float penAB = 0.0f;
		const bool hitAB = TestBoundaryPolygonPolygonOneWay(a.WorldPoints, b.WorldPoints, nAB, pAB, penAB);

		Vector2<float> nBA;
		Vector2<float> pBA;
		float penBA = 0.0f;
		const bool hitBA = TestBoundaryPolygonPolygonOneWay(b.WorldPoints, a.WorldPoints, nBA, pBA, penBA);

		if (!hitAB && !hitBA)
		{
			return false;
		}

		if (hitAB && (!hitBA || penAB <= penBA))
		{
			outNormal = nAB;
			outContactPoint = pAB;
			outPenetration = penAB;
		}
		else
		{
			outNormal = -nBA;
			outContactPoint = pBA;
			outPenetration = penBA;
		}
		return outPenetration > 0.0f;
	}

	bool TestCircleCircle(const CircleCollider2D& a, const CircleCollider2D& b,
	                      Vector2<float>& outNormal, Vector2<float>& outContactPoint,
	                      float& outPenetration)
	{
		const Vector2<float> delta      = b.WorldCenter - a.WorldCenter;
		const float          distanceSq = delta.LengthSqrt();
		const float          radiusSum  = a.WorldRadius + b.WorldRadius;
		if (distanceSq > radiusSum * radiusSum)
		{
			return false;
		}

		const float distance = std::sqrt(distanceSq);
		outNormal       = (distance > 0.0f) ? delta / distance : Vector2<float>(1.0f, 0.0f);
		outContactPoint = a.WorldCenter + outNormal * (a.WorldRadius - (radiusSum - distance) * 0.5f);
		outPenetration  = radiusSum - distance;
		return true;
	}

	// 볼록 폴리곤 점 목록과 원의 충돌 (SAT).
	// PolygonCollider2D 전체가 아닌 단일 볼록 점 집합을 받는 버전.
	bool TestConvexPointsCircle(const std::vector<Vector2<float>>& polyPts,
	                            const std::vector<bool>* boundaryEdges,
	                            const CircleCollider2D& circle,
	                            Vector2<float>& outNormal, float& outPenetration)
	{
		if (polyPts.size() < 3) return false;

		outPenetration = std::numeric_limits<float>::max();
		bool bestAxisIsBoundary = true;

		Vector2<float> closestPt  = polyPts[0];
		float          closestDSq = std::numeric_limits<float>::max();

		for (std::size_t i = 0; i < polyPts.size(); ++i)
		{
			const Vector2<float>& p0 = polyPts[i];
			const Vector2<float>& p1 = polyPts[(i + 1) % polyPts.size()];

			const Vector2<float> segPt = ClosestPointOnSegment(circle.WorldCenter, p0, p1);
			const float          dSq   = (circle.WorldCenter - segPt).LengthSqrt();
			if (dSq < closestDSq) { closestDSq = dSq; closestPt = segPt; }

			const Vector2<float> edge      = p1 - p0;
			const Vector2<float> perpRaw(-edge.y, edge.x);
			const float          perpLenSq = perpRaw.LengthSqrt();
			if (perpLenSq <= MIN_NORMAL_LENGTH_SQ) continue;

			const Vector2<float> axis = perpRaw * (1.0f / std::sqrt(perpLenSq));
			float minP, maxP;
			ProjectPolygon(polyPts, axis, minP, maxP);
			const float cProj   = Dot(circle.WorldCenter, axis);
			const float overlap = std::min(maxP, cProj + circle.WorldRadius)
			                    - std::max(minP, cProj - circle.WorldRadius);
			if (overlap <= 0.0f) return false;
			if (overlap < outPenetration)
			{
				outPenetration = overlap;
				outNormal = axis;
				bestAxisIsBoundary = IsBoundaryAxis(boundaryEdges, i);
			}
		}

		const Vector2<float> vAxisRaw = circle.WorldCenter - closestPt;
		if (vAxisRaw.LengthSqrt() > MIN_NORMAL_LENGTH_SQ)
		{
			const Vector2<float> vAxis = NormalizeSafe(vAxisRaw);
			float minP, maxP;
			ProjectPolygon(polyPts, vAxis, minP, maxP);
			const float cProj   = Dot(circle.WorldCenter, vAxis);
			const float overlap = std::min(maxP, cProj + circle.WorldRadius)
			                    - std::max(minP, cProj - circle.WorldRadius);
			if (overlap <= 0.0f) return false;
			if (overlap < outPenetration)
			{
				outPenetration = overlap;
				outNormal = vAxis;
				bestAxisIsBoundary = true;
			}
		}
		return bestAxisIsBoundary;
	}

	bool TestPolygonCircle(const PolygonCollider2D& polygon, const CircleCollider2D& circle,
	                       Vector2<float>& outNormal, float& outPenetration)
	{
		if (polygon.WorldPoints.size() < 3)
		{
			return false;
		}

		outPenetration = std::numeric_limits<float>::max();

		Vector2<float> closestPolygonPoint = polygon.WorldPoints[0];
		float          closestDistanceSq   = std::numeric_limits<float>::max();

		for (std::size_t i = 0; i < polygon.WorldPoints.size(); ++i)
		{
			const Vector2<float>& p0 = polygon.WorldPoints[i];
			const Vector2<float>& p1 = polygon.WorldPoints[(i + 1) % polygon.WorldPoints.size()];

			const Vector2<float> segPoint = ClosestPointOnSegment(circle.WorldCenter, p0, p1);
			const float          segDistSq = (circle.WorldCenter - segPoint).LengthSqrt();
			if (segDistSq < closestDistanceSq)
			{
				closestDistanceSq   = segDistSq;
				closestPolygonPoint = segPoint;
			}

			const Vector2<float> edge      = p1 - p0;
			const Vector2<float> perpRaw(-edge.y, edge.x);
			const float          perpLenSq = perpRaw.LengthSqrt();
			if (perpLenSq <= MIN_NORMAL_LENGTH_SQ)
			{
				continue;
			}
			const Vector2<float> axis = perpRaw * (1.0f / std::sqrt(perpLenSq));

			float minP, maxP;
			ProjectPolygon(polygon.WorldPoints, axis, minP, maxP);
			const float circleProj = Dot(circle.WorldCenter, axis);
			const float overlap    = std::min(maxP, circleProj + circle.WorldRadius)
			                       - std::max(minP, circleProj - circle.WorldRadius);
			if (overlap <= 0.0f)
			{
				return false;
			}
			if (overlap < outPenetration)
			{
				outPenetration = overlap;
				outNormal      = axis;
			}
		}

		// 가장 가까운 폴리곤 점 → 원 중심 축 (꼭짓점 접촉 처리)
		const Vector2<float> vertexAxisRaw = circle.WorldCenter - closestPolygonPoint;
		if (vertexAxisRaw.LengthSqrt() > MIN_NORMAL_LENGTH_SQ)
		{
			const Vector2<float> vertexAxis = NormalizeSafe(vertexAxisRaw);
			float minP, maxP;
			ProjectPolygon(polygon.WorldPoints, vertexAxis, minP, maxP);
			const float circleProj = Dot(circle.WorldCenter, vertexAxis);
			const float overlap    = std::min(maxP, circleProj + circle.WorldRadius)
			                       - std::max(minP, circleProj - circle.WorldRadius);
			if (overlap <= 0.0f)
			{
				return false;
			}
			if (overlap < outPenetration)
			{
				outPenetration = overlap;
				outNormal      = vertexAxis;
			}
		}

		// 접촉점은 반드시 OrientNormal 이후에 외부에서 계산한다 (6순위 수정).
		// 이 함수는 법선과 침투 깊이만 반환한다.
		return true;
	}

	// ── 폴리곤 관성 ───────────────────────────────────────────────────────────────

	float CalculatePolygonInertia(const std::vector<Vector2<float>>& localPoints, float mass)
	{
		if (localPoints.size() < 3 || mass <= 0.0f)
		{
			return mass;
		}

		float area    = 0.0f;
		float inertia = 0.0f;
		const std::size_t N = localPoints.size();

		for (std::size_t i = 0; i < N; ++i)
		{
			const Vector2<float>& p0    = localPoints[i];
			const Vector2<float>& p1    = localPoints[(i + 1) % N];
			const float           cross = std::abs(Cross(p0, p1));
			area    += cross;
			inertia += cross * (p0.x * p0.x + p0.y * p0.y
			                  + p0.x * p1.x + p0.y * p1.y
			                  + p1.x * p1.x + p1.y * p1.y);
		}

		area *= 0.5f;
		return (area > 0.0f) ? (mass / 6.0f) * inertia / area : mass;
	}

	// ── Rigidbody 헬퍼 ────────────────────────────────────────────────────────────

	float GetInverseMass(const Rigidbody2D* body)
	{
		if (nullptr == body || !body->IsEnabled
		    || EPhysics2DBodyType::Dynamic != body->BodyType || body->Mass <= 0.0f)
		{
			return 0.0f;
		}
		return 1.0f / body->Mass;
	}

	float GetInverseInertia(const Rigidbody2D* body)
	{
		if (nullptr == body || !body->IsEnabled
		    || EPhysics2DBodyType::Dynamic != body->BodyType
		    || body->FreezeRotation || body->Inertia <= 0.0f)
		{
			return 0.0f;
		}
		return 1.0f / body->Inertia;
	}

	float GetSurfaceFriction(const Rigidbody2D* body)
	{
		return body ? std::max(0.0f, body->Friction) : 1.0f;
	}

	void ApplyVelocityToTransform(const CScene& scene, EntityId entity, Transform2D& transform, Rigidbody2D& body, float dt)
	{
		Vector2<float> worldDelta = body.Velocity * dt;
		if (body.FreezePositionX) { worldDelta.x = 0.0f; body.Velocity.x = 0.0f; }
		if (body.FreezePositionY) { worldDelta.y = 0.0f; body.Velocity.y = 0.0f; }
		transform.Position += WorldVectorToParentLocal(scene, entity, worldDelta);

		if (body.FreezeRotation)
		{
			body.AngularVelocity = 0.0f;
			body.Torque          = 0.0f;
			return;
		}
		transform.RotationRadians += body.AngularVelocity * dt;
	}

	void ApplyCorrectionToTransform(const CScene& scene, EntityId entity, Transform2D& transform, Rigidbody2D* body,
	                                const Vector2<float>& correction)
	{
		Vector2<float> worldDelta = correction;
		if (body)
		{
			if (body->FreezePositionX) worldDelta.x = 0.0f;
			if (body->FreezePositionY) worldDelta.y = 0.0f;
		}
		transform.Position += WorldVectorToParentLocal(scene, entity, worldDelta);
	}

	Vector2<float> GetBodyWorldCenter(const CScene& scene, EntityId entity)
	{
		const Matrix3x2 worldTransform = CalculateWorldTransformNow(scene, entity);
		return worldTransform.TransformPoint(Vector2<float>(0.0f, 0.0f));
	}

	Vector2<float> GetContactVelocity(const Rigidbody2D* body,
	                                  const Vector2<float>& bodyCenter,
	                                  const Vector2<float>& contactPoint)
	{
		if (nullptr == body)
		{
			return Vector2<float>(0.0f, 0.0f);
		}
		return body->Velocity + Cross(body->AngularVelocity, contactPoint - bodyCenter);
	}

	void AddImpulseDebug(Rigidbody2D* body, const Vector2<float>& bodyCenter,
	                     const Vector2<float>& impulse,
	                     const Vector2<float>& contactNormal,
	                     const Vector2<float>& contactPoint,
	                     bool isFriction)
	{
		if (nullptr == body || !body->IsEnabled || EPhysics2DBodyType::Dynamic != body->BodyType)
		{
			return;
		}
		body->LastContactNormal   = contactNormal;
		body->LastContactPoint    = contactPoint;
		body->LastAngularImpulse += Cross(contactPoint - bodyCenter, impulse);
		if (isFriction)
		{
			body->LastFrictionImpulse += impulse.Length();
		}
		else
		{
			body->LastContactCount  += 1;
			body->LastNormalImpulse += impulse.Length();
		}
	}

	void ApplyImpulse(Rigidbody2D* body, const Vector2<float>& bodyCenter,
	                  const Vector2<float>& impulse, const Vector2<float>& contactPoint)
	{
		if (nullptr == body || !body->IsEnabled || EPhysics2DBodyType::Dynamic != body->BodyType)
		{
			return;
		}

		const float inverseMass = GetInverseMass(body);
		if (inverseMass > 0.0f)
		{
			Vector2<float> linear = impulse * inverseMass;
			if (body->FreezePositionX) linear.x = 0.0f;
			if (body->FreezePositionY) linear.y = 0.0f;
			body->Velocity += linear;
		}

		const float inverseInertia = GetInverseInertia(body);
		if (inverseInertia > 0.0f)
		{
			body->AngularVelocity += Cross(contactPoint - bodyCenter, impulse) * inverseInertia;
		}
	}

	// ── 접촉 매니폴드 (엣지 클리핑) ──────────────────────────────────────────────

	struct ContactManifold2D
	{
		Vector2<float> Points[2];
		int            Count = 0;
	};

	int ClipSegment(const Vector2<float>& p0, const Vector2<float>& p1,
	                const Vector2<float>& planeNormal, float planeOffset,
	                Vector2<float> out[2])
	{
		const float d0 = Dot(p0, planeNormal) - planeOffset;
		const float d1 = Dot(p1, planeNormal) - planeOffset;
		int k = 0;
		if (d0 >= 0.0f)                   out[k++] = p0;
		if ((d0 < 0.0f) != (d1 < 0.0f))  out[k++] = p0 + (p1 - p0) * (d0 / (d0 - d1));
		if (d1 >= 0.0f && k < 2)          out[k++] = p1;
		return k;
	}

	Vector2<float> GetFaceOutwardNormal(const std::vector<Vector2<float>>& poly,
	                                    const Vector2<float>& polyCenter, int i)
	{
		const int            N     = static_cast<int>(poly.size());
		const Vector2<float> edge  = poly[(i + 1) % N] - poly[i];
		const Vector2<float> leftN(-edge.y, edge.x);
		const Vector2<float> mid  = (poly[i] + poly[(i + 1) % N]) * 0.5f;
		return (Dot(leftN, mid - polyCenter) >= 0.0f) ? leftN : -leftN;
	}

	ContactManifold2D BuildPolygonManifold(const std::vector<Vector2<float>>& a,
	                                       const std::vector<Vector2<float>>& b,
	                                       const Vector2<float>& normal)
	{
		const int NA = static_cast<int>(a.size());
		const int NB = static_cast<int>(b.size());

		// fallback: A에서 normal 방향 지지점, B에서 -normal 방향 지지점의 중점 (7순위 수정)
		auto Fallback = [&]() -> ContactManifold2D
		{
			ContactManifold2D m;
			m.Points[0] = (SupportPoint(a, normal) + SupportPoint(b, -normal)) * 0.5f;
			m.Count     = 1;
			return m;
		};

		if (NA < 2 || NB < 2) return Fallback();

		const Vector2<float> centerA = CalculateCenter(a);
		const Vector2<float> centerB = CalculateCenter(b);

		// 1. A에서 reference face 선택 (normal과 가장 정렬된 면)
		float bestDot = -std::numeric_limits<float>::max();
		int   refIdx  = 0;
		for (int i = 0; i < NA; ++i)
		{
			const float d = Dot(GetFaceOutwardNormal(a, centerA, i), normal);
			if (d > bestDot) { bestDot = d; refIdx = i; }
		}
		const Vector2<float>& rv0    = a[refIdx];
		const Vector2<float>& rv1    = a[(refIdx + 1) % NA];
		const Vector2<float>  rEdge  = rv1 - rv0;
		const float           rLenSq = rEdge.LengthSqrt();
		if (rLenSq <= MIN_NORMAL_LENGTH_SQ) return Fallback();
		const Vector2<float> rDir = rEdge * (1.0f / std::sqrt(rLenSq));

		// 2. B에서 incident face 선택 (normal과 가장 반대인 면)
		float worstDot = std::numeric_limits<float>::max();
		int   incIdx   = 0;
		for (int i = 0; i < NB; ++i)
		{
			const float d = Dot(GetFaceOutwardNormal(b, centerB, i), normal);
			if (d < worstDot) { worstDot = d; incIdx = i; }
		}
		Vector2<float> seg[2] = { b[incIdx], b[(incIdx + 1) % NB] };
		int segCount = 2;

		// 3. 두 측면 평면으로 incident 선분 클리핑
		{
			Vector2<float> tmp[2];
			const int cnt = ClipSegment(seg[0], seg[1], rDir, Dot(rDir, rv0), tmp);
			segCount = cnt;
			if (cnt > 0) seg[0] = tmp[0];
			if (cnt > 1) seg[1] = tmp[1];
		}
		if (segCount == 0) return Fallback();

		{
			Vector2<float> tmp[2];
			int cnt = 0;
			if (segCount == 2)
			{
				cnt = ClipSegment(seg[0], seg[1], -rDir, Dot(-rDir, rv1), tmp);
			}
			else
			{
				const float d = Dot(-rDir, seg[0]) - Dot(-rDir, rv1);
				if (d >= 0.0f) { tmp[cnt++] = seg[0]; }
			}
			segCount = cnt;
			if (cnt > 0) seg[0] = tmp[0];
			if (cnt > 1) seg[1] = tmp[1];
		}
		if (segCount == 0) return Fallback();

		// 4. reference face 뒤쪽에 있는 점만 유효한 접촉점으로 채택
		const Vector2<float> rNormRaw = GetFaceOutwardNormal(a, centerA, refIdx);
		const float          rNormLen = std::sqrt(rNormRaw.LengthSqrt());
		const Vector2<float> rNorm    = (rNormLen > 1e-6f) ? rNormRaw * (1.0f / rNormLen) : normal;
		const float          refOffset = Dot(rNorm, rv0);

		ContactManifold2D manifold;
		for (int i = 0; i < segCount; ++i)
		{
			if (Dot(rNorm, seg[i]) - refOffset <= 0.02f && manifold.Count < 2)
			{
				manifold.Points[manifold.Count++] = seg[i];
			}
		}

		return manifold.Count > 0 ? manifold : Fallback();
	}

	// ── Ear Clipping 삼각분할 ────────────────────────────────────────────────────
	//
	// 오목/볼록 폴리곤을 삼각형 목록으로 분해한다.
	// 폴리곤 가정: 단순 폴리곤 (자기교차 없음), 3 꼭짓점 이상.
	//
	// 알고리즘 개요 (O(n²)):
	//   1. 부호 있는 넓이 계산 → CW 이면 인덱스 반전(CCW 로 정규화)
	//   2. 반복: 현재 귀(ear)를 찾아 삼각형으로 분리, 인덱스 목록 갱신
	//   귀 판정: (1) 볼록 꼭짓점 (내부각 < π), (2) 나머지 꼭짓점이 삼각형 내부에 없음
	//
	// outTriangles: [A0, B0, C0,  A1, B1, C1, ...] 형식으로 꼭짓점을 기록한다.

	// 부호 있는 넓이 (> 0 이면 CCW)
	float SignedArea(const std::vector<Vector2<float>>& pts, const std::vector<int>& idx)
	{
		float area = 0.0f;
		const int n = static_cast<int>(idx.size());
		for (int i = 0; i < n; ++i)
		{
			const Vector2<float>& p0 = pts[idx[i]];
			const Vector2<float>& p1 = pts[idx[(i + 1) % n]];
			area += p0.x * p1.y - p1.x * p0.y;
		}
		return area * 0.5f;
	}

	// 점 P가 삼각형 ABC 안에 있는지 (경계 포함)
	bool PointInTriangle(const Vector2<float>& p,
	                     const Vector2<float>& a,
	                     const Vector2<float>& b,
	                     const Vector2<float>& c)
	{
		const float d1 = Cross(b - a, p - a);
		const float d2 = Cross(c - b, p - b);
		const float d3 = Cross(a - c, p - c);
		const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
		const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
		return !(hasNeg && hasPos);
	}

	bool PointsEqual(const Vector2<float>& a, const Vector2<float>& b)
	{
		constexpr float POINT_EPSILON = 0.0001f;
		return std::abs(a.x - b.x) <= POINT_EPSILON
			&& std::abs(a.y - b.y) <= POINT_EPSILON;
	}

	bool IsOriginalPolygonEdge(const std::vector<Vector2<float>>& polygon,
	                           const Vector2<float>& a, const Vector2<float>& b)
	{
		if (polygon.size() < 2)
		{
			return false;
		}

		for (std::size_t i = 0; i < polygon.size(); ++i)
		{
			const Vector2<float>& p0 = polygon[i];
			const Vector2<float>& p1 = polygon[(i + 1) % polygon.size()];
			if ((PointsEqual(a, p0) && PointsEqual(b, p1))
			    || (PointsEqual(a, p1) && PointsEqual(b, p0)))
			{
				return true;
			}
		}
		return false;
	}

	void TriangulateEarClip(
		const std::vector<Vector2<float>>& pts,
		std::vector<std::array<Vector2<float>, 3>>& outTriangles)
	{
		const int n = static_cast<int>(pts.size());
		if (n < 3) return;

		// 인덱스 배열 (CCW 순서로 정규화)
		std::vector<int> idx(n);
		for (int i = 0; i < n; ++i) idx[i] = i;

		// CW 이면 반전 → CCW 보장. CCW 기준 볼록 꼭짓점은 Cross > 0.
		if (SignedArea(pts, idx) < 0.0f)
			std::reverse(idx.begin(), idx.end());

		int remaining = n;
		int safety    = n * n + 4; // 무한 루프 방지

		while (remaining > 3 && safety-- > 0)
		{
			bool found = false;
			for (int i = 0; i < remaining; ++i)
			{
				const int prev = (i + remaining - 1) % remaining;
				const int next = (i + 1) % remaining;

				const Vector2<float>& A = pts[idx[prev]];
				const Vector2<float>& B = pts[idx[i]];
				const Vector2<float>& C = pts[idx[next]];

				// 볼록 꼭짓점 판정 (CCW 기준: Cross(AB, BC) > 0)
				const float cross = Cross(B - A, C - B);
				if (cross <= MIN_NORMAL_LENGTH_SQ) continue;

				// 나머지 꼭짓점이 삼각형 ABC 안에 있으면 귀가 아님
				bool isEar = true;
				for (int k = 0; k < remaining; ++k)
				{
					if (k == prev || k == i || k == next) continue;
					if (PointInTriangle(pts[idx[k]], A, B, C))
					{
						isEar = false;
						break;
					}
				}

				if (!isEar) continue;

				// 귀 삼각형 추가
				outTriangles.push_back({ A, B, C });

				// 인덱스 배열에서 귀 꼭짓점 제거
				idx.erase(idx.begin() + i);
				--remaining;
				found = true;
				break;
			}

			// 귀를 찾지 못하면 (자기교차 폴리곤 등) 탈출
			if (!found) break;
		}

		// 마지막 삼각형
		if (remaining == 3)
		{
			outTriangles.push_back({ pts[idx[0]], pts[idx[1]], pts[idx[2]] });
		}
	}

	// PolygonCollider2D 의 LocalPoints 로부터 ConvexPieces(삼각형) 를 재빌드한다.
	// WorldPoints 갱신은 UpdateColliderBounds Pass 1a 에서 별도로 수행한다.
	void RebuildConvexPieces(PolygonCollider2D& collider)
	{
		collider.ConvexPieces.clear();
		if (collider.LocalPoints.size() < 3) return;

		std::vector<std::array<Vector2<float>, 3>> tris;
		TriangulateEarClip(collider.LocalPoints, tris);

		collider.ConvexPieces.reserve(tris.size());
		for (const auto& tri : tris)
		{
			ConvexPiece2D piece;
			piece.LocalPoints = { tri[0], tri[1], tri[2] };
			piece.WorldPoints.resize(3); // 월드 좌표는 UpdateColliderBounds 에서 채움
			piece.BoundaryEdges.reserve(3);
			for (int edge = 0; edge < 3; ++edge)
			{
				piece.BoundaryEdges.push_back(IsOriginalPolygonEdge(
					collider.LocalPoints,
					piece.LocalPoints[edge],
					piece.LocalPoints[(edge + 1) % 3]));
			}
			collider.ConvexPieces.push_back(std::move(piece));
		}

		if (collider.ConvexPieces.empty())
		{
			ConvexPiece2D fallback;
			fallback.LocalPoints = collider.LocalPoints;
			fallback.WorldPoints.resize(collider.LocalPoints.size());
			fallback.BoundaryEdges.assign(collider.LocalPoints.size(), true);
			collider.ConvexPieces.push_back(std::move(fallback));
		}
	}

} // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────────────

void CPhysics2DSystem::SetGravity(const Vector2<float>& gravity)             { m_gravity = gravity; }
const Vector2<float>& CPhysics2DSystem::GetGravity() const                  { return m_gravity; }

void CPhysics2DSystem::SetVelocityIterations(int it)  { m_velocityIterations = std::max(1, it); }
int  CPhysics2DSystem::GetVelocityIterations() const  { return m_velocityIterations; }

void CPhysics2DSystem::SetPositionIterations(int it)  { m_positionIterations = std::max(1, it); }
int  CPhysics2DSystem::GetPositionIterations() const  { return m_positionIterations; }

void CPhysics2DSystem::SetNumSubSteps(int steps)      { m_numSubSteps = std::max(1, steps); }
int  CPhysics2DSystem::GetNumSubSteps() const         { return m_numSubSteps; }

void  CPhysics2DSystem::SetMaxLinearVelocity(float v) { m_maxLinearVelocity = std::max(1.0f, v); }
float CPhysics2DSystem::GetMaxLinearVelocity() const  { return m_maxLinearVelocity; }

const std::vector<Physics2DManifold>& CPhysics2DSystem::GetManifolds() const { return m_manifolds; }

// ── Fixed-step 진입점 ─────────────────────────────────────────────────────────

void CPhysics2DSystem::OnFixedUpdate(CScene& scene)
{
	const float fixedDelta = Core::Time ? Core::Time->GetFixedDeltaSeconds() : 0.02f;
	if (fixedDelta < MIN_PHYSICS_DELTA_SECONDS)
	{
		UpdateColliderBounds(scene);
		DetectContacts(scene);
		return;
	}

	// Sub-step: 고정 스텝을 N등분한다.
	//   - 각 스텝의 이동량이 1/N으로 줄어 감지 시점의 초기 침투량이 줄어든다.
	//   - Baumgarte bias = β/dt × penetration 이므로 dt가 작아질수록 교정력이 강해진다.
	//   - 둘이 결합하면 "침투 발생 자체가 감소" + "발생한 침투의 수렴이 빨라짐" 두 효과를 동시에 얻는다.
	const int   numSubSteps = std::max(1, m_numSubSteps);
	const float subDt       = fixedDelta / static_cast<float>(numSubSteps);
	for (int sub = 0; sub < numSubSteps; ++sub)
	{
		Step(scene, subDt);
	}
}

void CPhysics2DSystem::Step(CScene& scene, float deltaSeconds)
{
	IntegrateBodies(scene, deltaSeconds);
	UpdateColliderBounds(scene);
	DetectContacts(scene);

	for (int i = 0; i < m_velocityIterations; ++i)
	{
		ResolveContactVelocity(scene);
	}

	for (int i = 0; i < m_positionIterations; ++i)
	{
		ResolveContactPosition(scene);
	}

	StabilizeRestingContacts(scene);

	// 스텝 종료 후 collider/매니폴드 재빌드.
	// 스크립트, 디버그 렌더링, 다음 FixedUpdate 쿼리에서 최신 상태를 읽는다.
	UpdateColliderBounds(scene);
	DetectContacts(scene);
}

// ── 적분 ──────────────────────────────────────────────────────────────────────

void CPhysics2DSystem::IntegrateBodies(CScene& scene, float deltaSeconds)
{
	scene.ForEach<Transform2D, Rigidbody2D>([this, &scene, deltaSeconds](EntityId entity, Transform2D& transform, Rigidbody2D& body)
	{
		body.LastContactCount    = 0;
		body.LastContactNormal   = Vector2<float>(0.0f, 0.0f);
		body.LastContactPoint    = Vector2<float>(0.0f, 0.0f);
		body.LastNormalImpulse   = 0.0f;
		body.LastFrictionImpulse = 0.0f;
		body.LastAngularImpulse  = 0.0f;

		if (!body.IsEnabled || EPhysics2DBodyType::Static == body.BodyType)
		{
			return;
		}

		if (EPhysics2DBodyType::Kinematic == body.BodyType)
		{
			ApplyVelocityToTransform(scene, entity, transform, body, deltaSeconds);
			return;
		}

		const float invMass = GetInverseMass(&body);
		if (invMass <= 0.0f) return;

		if (body.UseGravity)
		{
			body.Velocity += m_gravity * body.GravityScale * deltaSeconds;
		}
		body.Velocity += body.Force * invMass * deltaSeconds;
		if (body.LinearDamping > 0.0f)
		{
			body.Velocity *= std::max(0.0f, 1.0f - body.LinearDamping * deltaSeconds);
		}
		if (!body.FreezeRotation)
		{
			const float invInertia = GetInverseInertia(&body);
			if (invInertia > 0.0f)
			{
				body.AngularVelocity += body.Torque * invInertia * deltaSeconds;
			}
			if (body.AngularDamping > 0.0f)
			{
				body.AngularVelocity *= std::max(0.0f, 1.0f - body.AngularDamping * deltaSeconds);
			}
		}

		// ── 터널링 방지: 최대 선속도 클램프 ──────────────────────────────────────
		// 한 서브스텝에서 이동 거리 = |Velocity| * dt 가 최소 충돌체보다 크면 SAT가 겹침을
		// 감지하지 못해 벽을 뚫는다.  속도 크기를 m_maxLinearVelocity 로 제한한다.
		{
			const float speedSq = body.Velocity.LengthSqrt();
			const float maxSq   = m_maxLinearVelocity * m_maxLinearVelocity;
			if (speedSq > maxSq)
			{
				body.Velocity = body.Velocity * (m_maxLinearVelocity / std::sqrt(speedSq));
			}
		}

		ApplyVelocityToTransform(scene, entity, transform, body, deltaSeconds);
		body.Force  = Vector2<float>(0.0f, 0.0f);
		body.Torque = 0.0f;
	});
}

// ── Collider 경계 업데이트 ────────────────────────────────────────────────────

void CPhysics2DSystem::UpdateColliderBounds(CScene& scene)
{
	// Pass 1a: 폴리곤 월드 기하
	scene.ForEach<PolygonCollider2D>([&scene](EntityId entity, PolygonCollider2D& collider)
	{
		if (!collider.IsEnabled)
		{
			collider.WorldPoints.clear();
			collider.ConvexPieces.clear();
			return;
		}
		const Matrix3x2 wt = CalculateWorldTransformNow(scene, entity);

		// ── 절차적 재빌드 ────────────────────────────────────────────────────────
		// 절차적 파라미터(VertexCount/Size/Rotation/Center)가 변경된 경우에만 재빌드.
		// 에디터가 LocalPoints 를 직접 편집한 경우 파라미터가 바뀌지 않으면 덮어쓰지 않는다.
		if (collider.NeedsProceduralRebuild())
		{
			collider.BuildLocalPoints(collider.LocalPoints);
			collider.MarkProceduralBuilt();
			collider.m_convexDirty = true; // LocalPoints 바뀜 → 재분해 필요
		}

		const std::uint64_t localPointsHash = HashLocalPoints(collider.LocalPoints);

		// ── 볼록 분해 재빌드 ──────────────────────────────────────────────────────
		// LocalPoints 가 에디터/직렬화/절차적 재빌드로 변경됐을 때 수행.
		if (collider.m_convexDirty || collider.m_builtLocalPointsHash != localPointsHash)
		{
			RebuildConvexPieces(collider);
			collider.m_builtLocalPointsHash = localPointsHash;
			collider.m_convexDirty = false;
		}

		// ── 전체 WorldPoints 갱신 ────────────────────────────────────────────────
		collider.WorldPoints.clear();
		collider.WorldPoints.reserve(collider.LocalPoints.size());
		for (const Vector2<float>& p : collider.LocalPoints)
		{
			collider.WorldPoints.push_back(wt.TransformPoint(p));
		}
		collider.WorldAABB = CalculateAABB(collider.WorldPoints);

		// ── ConvexPiece 월드 좌표 갱신 ──────────────────────────────────────────
		for (ConvexPiece2D& piece : collider.ConvexPieces)
		{
			const std::size_t ptCount = piece.LocalPoints.size();
			piece.WorldPoints.resize(ptCount);
			for (std::size_t k = 0; k < ptCount; ++k)
			{
				piece.WorldPoints[k] = wt.TransformPoint(piece.LocalPoints[k]);
			}
			piece.WorldBounds = CalculateAABB(piece.WorldPoints);
		}
	});

	// Pass 1b: 원 월드 기하
	scene.ForEach<CircleCollider2D>([&scene](EntityId entity, CircleCollider2D& collider)
	{
		if (!collider.IsEnabled) return;
		const Matrix3x2 wt    = CalculateWorldTransformNow(scene, entity);
		collider.WorldCenter  = wt.TransformPoint(Vector2<float>(0.0f, 0.0f));
		const float scaleX    = std::sqrt(wt.M11 * wt.M11 + wt.M12 * wt.M12);
		const float scaleY    = std::sqrt(wt.M21 * wt.M21 + wt.M22 * wt.M22);
		collider.WorldRadius  = collider.Radius * std::max(scaleX, scaleY);
		const float r         = collider.WorldRadius;
		collider.WorldAABB.Min = Vector2<float>(collider.WorldCenter.x - r, collider.WorldCenter.y - r);
		collider.WorldAABB.Max = Vector2<float>(collider.WorldCenter.x + r, collider.WorldCenter.y + r);
	});

	// Pass 2: 동적 Rigidbody 관성 누산 — 엔티티의 모든 콜라이더 기여 합산
	scene.ForEach<Rigidbody2D>([&scene](EntityId entity, Rigidbody2D& body)
	{
		if (!body.IsEnabled || EPhysics2DBodyType::Dynamic != body.BodyType || body.Mass <= 0.0f)
		{
			return;
		}

		float totalInertia = 0.0f;
		bool  hasShape     = false;

		for (PolygonCollider2D* poly : scene.GetAllComponents<PolygonCollider2D>(entity))
		{
			if (poly && poly->IsEnabled && !poly->LocalPoints.empty())
			{
				const float contrib = CalculatePolygonInertia(poly->LocalPoints, body.Mass);
				if (contrib > 0.0f) { totalInertia += contrib; hasShape = true; }
			}
		}
		for (CircleCollider2D* circle : scene.GetAllComponents<CircleCollider2D>(entity))
		{
			if (circle && circle->IsEnabled && circle->WorldRadius > 0.0f)
			{
				totalInertia += 0.5f * body.Mass * circle->WorldRadius * circle->WorldRadius;
				hasShape = true;
			}
		}

		if (hasShape && totalInertia > 0.0f)
		{
			body.SetInertia(totalInertia);
		}
	});
}

// ── 접촉 감지 ─────────────────────────────────────────────────────────────────
//
// 결과는 Physics2DManifold 목록으로 저장.
// 한 충돌 쌍(A, B)은 반드시 매니폴드 1개 — 접촉점이 2개여도 매니폴드는 1개.
// 이로써 position solver가 쌍마다 정확히 1회 보정을 수행할 수 있다.

void CPhysics2DSystem::DetectContacts(CScene& scene)
{
	m_manifolds.clear();

	std::vector<std::pair<EntityId, PolygonCollider2D*>> polygons;
	scene.ForEach<PolygonCollider2D>([&polygons](EntityId entity, PolygonCollider2D& c)
	{
		if (c.IsEnabled && c.WorldPoints.size() >= 3)
		{
			polygons.emplace_back(entity, &c);
		}
	});

	std::vector<std::pair<EntityId, CircleCollider2D*>> circles;
	scene.ForEach<CircleCollider2D>([&circles](EntityId entity, CircleCollider2D& c)
	{
		if (c.IsEnabled)
		{
			circles.emplace_back(entity, &c);
		}
	});

	// ── Polygon vs Polygon ────────────────────────────────────────────────────
	//
	// 볼록 다각형: WorldPoints 전체로 직접 SAT — 정확하고 빠름.
	// 오목 다각형: ConvexPiece(삼각형) 쌍별 SAT — BoundaryEdge 필터링 없음.
	//   ▸ BoundaryEdge 필터링을 쓰면 내부 대각선이 최소 침투 축일 때 충돌을 잘못 기각했음.
	//   ▸ ConvexPiece 삼각형의 모든 엣지는 해당 삼각형에 대해 유효한 분리 축이므로
	//     필터링 없이 SAT 를 수행하면 됨. 최소 침투 쌍을 대표 매니폴드로 채택.
	//
	// ★ 제거된 설계(TestBoundaryPolygonPolygon)의 SLOP*2 버그:
	//   엣지 교차 케이스에서 침투 깊이를 SLOP*2(=0.01) 로 고정 → 위치 보정이 너무 작아
	//   캐릭터가 바닥을 뚫는 것처럼 보이는 현상이 있었음.
	for (std::size_t i = 0; i < polygons.size(); ++i)
	{
		for (std::size_t j = i + 1; j < polygons.size(); ++j)
		{
			const EntityId ea = polygons[i].first;
			const EntityId eb = polygons[j].first;
			if (ea == eb) continue;

			PolygonCollider2D* a = polygons[i].second;
			PolygonCollider2D* b = polygons[j].second;
			if (!IntersectsAABB(a->WorldAABB, b->WorldAABB)) continue;

			const bool aIsConvex = IsConvexPolygon(a->WorldPoints);
			const bool bIsConvex = IsConvexPolygon(b->WorldPoints);

			Vector2<float> bestNormal;
			float          bestPen = 0.0f;
			bool           hit     = false;

			if (aIsConvex && bIsConvex)
			{
				// ── 볼록 × 볼록: 전체 WorldPoints SAT ─────────────────────────────
				hit = TestPolygonSAT(a->WorldPoints, b->WorldPoints, bestNormal, bestPen);
			}
			else
			{
				// ── 오목 포함: 볼록/오목 여부에 따라 ConvexPiece/전체 WorldPoints 혼합 ──
				//
				// 핵심 원칙:
				//   볼록 폴리곤은 분해하지 않고 전체 WorldPoints 를 사용한다.
				//   볼록 폴리곤을 ConvexPiece 로 나누면, 일부 삼각형에 원래 폴리곤의
				//   "접촉면 엣지"(예: 바닥 상단 엣지)가 없어서 기울어진 동적 오브젝트의
				//   엣지가 최소 침투로 선택되고 수평 법선이 생성되는 버그가 발생한다.
				//   → 볼록인 쪽은 항상 전체 WorldPoints 를 SAT 에 넘겨야 한다.
				//
				// 케이스 분류:
				//   A 오목 + B 볼록 → ConvexPieces(A) × WorldPoints(B)
				//   A 볼록 + B 오목 → WorldPoints(A) × ConvexPieces(B)
				//   A 오목 + B 오목 → ConvexPieces(A) × ConvexPieces(B)
				//   ConvexPiece 없을 시 → 전체 WorldPoints 폴백

				const bool aHasPieces = !a->ConvexPieces.empty();
				const bool bHasPieces = !b->ConvexPieces.empty();

				if (!aIsConvex && bIsConvex)
				{
					// ── A 오목, B 볼록: ConvexPieces(A) × WorldPoints(B) ─────────
					if (!aHasPieces)
					{
						hit = TestPolygonSAT(a->WorldPoints, b->WorldPoints, bestNormal, bestPen);
					}
					else
					{
						float bestPenSoFar = 0.0f;
						for (const ConvexPiece2D& pa : a->ConvexPieces)
						{
							if (pa.WorldPoints.size() < 3) continue;
							if (!IntersectsAABB(pa.WorldBounds, b->WorldAABB)) continue;

							Vector2<float> n; float pen = 0.0f;
							// B 는 볼록 전체 → BoundaryEdges nullptr (전 엣지 경계)
							if (!TestPolygonSAT(pa.WorldPoints, b->WorldPoints,
							                   &pa.BoundaryEdges, nullptr, n, pen)) continue;

							if (!hit || pen < bestPenSoFar)
							{ bestPenSoFar = pen; bestNormal = n; bestPen = pen; hit = true; }
						}
					}
				}
				else if (aIsConvex && !bIsConvex)
				{
					// ── A 볼록, B 오목: WorldPoints(A) × ConvexPieces(B) ─────────
					if (!bHasPieces)
					{
						hit = TestPolygonSAT(a->WorldPoints, b->WorldPoints, bestNormal, bestPen);
					}
					else
					{
						float bestPenSoFar = 0.0f;
						for (const ConvexPiece2D& pb : b->ConvexPieces)
						{
							if (pb.WorldPoints.size() < 3) continue;
							if (!IntersectsAABB(a->WorldAABB, pb.WorldBounds)) continue;

							Vector2<float> n; float pen = 0.0f;
							// A 는 볼록 전체 → BoundaryEdges nullptr
							if (!TestPolygonSAT(a->WorldPoints, pb.WorldPoints,
							                   nullptr, &pb.BoundaryEdges, n, pen)) continue;

							if (!hit || pen < bestPenSoFar)
							{ bestPenSoFar = pen; bestNormal = n; bestPen = pen; hit = true; }
						}
					}
				}
				else
				{
					// ── A 오목 + B 오목: ConvexPieces(A) × ConvexPieces(B) ────────
					if (!aHasPieces || !bHasPieces)
					{
						hit = TestPolygonSAT(a->WorldPoints, b->WorldPoints, bestNormal, bestPen);
					}
					else
					{
						float bestPenSoFar = 0.0f;
						for (const ConvexPiece2D& pa : a->ConvexPieces)
						{
							if (pa.WorldPoints.size() < 3) continue;
							for (const ConvexPiece2D& pb : b->ConvexPieces)
							{
								if (pb.WorldPoints.size() < 3) continue;
								if (!IntersectsAABB(pa.WorldBounds, pb.WorldBounds)) continue;

								Vector2<float> n; float pen = 0.0f;
								if (!TestPolygonSAT(pa.WorldPoints, pb.WorldPoints,
								                   &pa.BoundaryEdges, &pb.BoundaryEdges, n, pen)) continue;

								if (!hit || pen < bestPenSoFar)
								{ bestPenSoFar = pen; bestNormal = n; bestPen = pen; hit = true; }
							}
						}
					}
				}
			}

			if (!hit) continue;

			const Vector2<float> centerA = CalculateCenter(a->WorldPoints);
			const Vector2<float> centerB = CalculateCenter(b->WorldPoints);
			OrientNormal(bestNormal, centerA, centerB);

			Physics2DManifold manifold;
			manifold.A           = ea;
			manifold.B           = eb;
			manifold.Normal      = bestNormal;
			manifold.Penetration = bestPen;
			manifold.IsTrigger   = a->IsTrigger || b->IsTrigger;

			const ContactManifold2D cm = BuildPolygonManifold(a->WorldPoints, b->WorldPoints, bestNormal);
			manifold.ContactCount = cm.Count;
			for (int mi = 0; mi < cm.Count; ++mi)
				manifold.ContactPoints[mi] = cm.Points[mi];

			m_manifolds.push_back(manifold);
		}
	}

	// ── Circle vs Circle ──────────────────────────────────────────────────────
	for (std::size_t i = 0; i < circles.size(); ++i)
	{
		for (std::size_t j = i + 1; j < circles.size(); ++j)
		{
			const EntityId ea = circles[i].first;
			const EntityId eb = circles[j].first;
			if (ea == eb) continue;

			CircleCollider2D* a = circles[i].second;
			CircleCollider2D* b = circles[j].second;
			if (!IntersectsAABB(a->WorldAABB, b->WorldAABB)) continue;

			Vector2<float> normal, contactPoint;
			float          penetration;
			if (!TestCircleCircle(*a, *b, normal, contactPoint, penetration)) continue;

			// TestCircleCircle이 반환한 normal은 이미 A→B 방향이지만 일관성을 위해 호출
			OrientNormal(normal, a->WorldCenter, b->WorldCenter);

			Physics2DManifold manifold;
			manifold.A                = ea;
			manifold.B                = eb;
			manifold.Normal           = normal;
			manifold.Penetration      = penetration;
			manifold.IsTrigger        = a->IsTrigger || b->IsTrigger;
			manifold.ContactPoints[0] = contactPoint;
			manifold.ContactCount     = 1;

			m_manifolds.push_back(manifold);
		}
	}

	// ── Polygon vs Circle ─────────────────────────────────────────────────────
	// 오목 폴리곤 지원: ConvexPiece 각각에 대해 원 충돌 검사, 최소 침투 채택.
	for (auto& [pe, polygon] : polygons)
	{
		for (auto& [ce, circle] : circles)
		{
			if (pe == ce) continue;
			if (!IntersectsAABB(polygon->WorldAABB, circle->WorldAABB)) continue;

			bool        bestHit  = false;
			float       bestPen  = std::numeric_limits<float>::max();
			Vector2<float> bestNormal;
			Vector2<float> bestContactPoint;

			if (IsConvexPolygon(polygon->WorldPoints))
			{
				// 볼록 폴리곤: 전체 WorldPoints 직접 SAT
				Vector2<float> n; float pen;
				if (TestPolygonCircle(*polygon, *circle, n, pen))
				{
					bestPen = pen; bestNormal = n; bestHit = true;
				}
			}
			else
			{
				// 오목 폴리곤: ConvexPiece 별 SAT (BoundaryEdge 필터링 없음)
				// TestBoundaryPolygonCircle 의 SLOP*2 고정 침투 깊이 버그를 피한다.
				for (const ConvexPiece2D& piece : polygon->ConvexPieces)
				{
					if (piece.WorldPoints.size() < 3) continue;
					if (!IntersectsAABB(piece.WorldBounds, circle->WorldAABB)) continue;

					Vector2<float> n; float pen;
					if (!TestConvexPointsCircle(piece.WorldPoints, &piece.BoundaryEdges, *circle, n, pen))
						continue;

					if (!bestHit || pen < bestPen)
					{
						bestPen    = pen;
						bestNormal = n;
						bestHit    = true;
					}
				}

				// ConvexPiece 없을 경우 폴백
				if (!bestHit && polygon->ConvexPieces.empty())
				{
					Vector2<float> n; float pen;
					if (TestPolygonCircle(*polygon, *circle, n, pen))
					{
						bestPen = pen; bestNormal = n; bestHit = true;
					}
				}
			}

			if (!bestHit) continue;

			const Vector2<float> centerA = CalculateCenter(polygon->WorldPoints);
			OrientNormal(bestNormal, centerA, circle->WorldCenter);

			Physics2DManifold manifold;
			manifold.A                = pe;
			manifold.B                = ce;
			manifold.Normal           = bestNormal;
			manifold.Penetration      = bestPen;
			manifold.IsTrigger        = polygon->IsTrigger || circle->IsTrigger;
			manifold.ContactPoints[0] = bestContactPoint.LengthSqrt() > MIN_NORMAL_LENGTH_SQ
				? bestContactPoint
				: circle->WorldCenter - bestNormal * circle->WorldRadius;
			manifold.ContactCount     = 1;

			m_manifolds.push_back(manifold);
		}
	}
}

// ── 속도 해소 ─────────────────────────────────────────────────────────────────
// Box2D / Godot / PhysX 공통 설계: velocity solver 는 순수하게 속도만 처리한다.
// 위치 오차(침투) 교정은 ResolveContactPosition 에서만 수행한다.
//
// velocity solver 에 position bias 를 섞으면:
//   - 교정 속도가 body.Velocity 에 영구 누적
//   - 정지 접촉에서 tiny velocity 가 제거되지 않아 한 방향으로 계속 drift 발생
//
// 매니폴드 단위로 순회하되, impulse 는 접촉점별로 독립 계산한다.
// 두 접촉점이 서로 다른 모멘트 암(ra, rb)을 가지므로 각각 계산이 회전 정확도에 유리하다.

void CPhysics2DSystem::ResolveContactVelocity(CScene& scene)
{
	for (const Physics2DManifold& manifold : m_manifolds)
	{
		if (manifold.IsTrigger || manifold.ContactCount <= 0) continue;

		Transform2D* transformA = scene.GetComponent<Transform2D>(manifold.A);
		Transform2D* transformB = scene.GetComponent<Transform2D>(manifold.B);
		if (!transformA || !transformB) continue;

		Rigidbody2D* bodyA = scene.GetComponent<Rigidbody2D>(manifold.A);
		Rigidbody2D* bodyB = scene.GetComponent<Rigidbody2D>(manifold.B);

		const Vector2<float> normal = NormalizeSafe(manifold.Normal);

		const float invMassA   = GetInverseMass(bodyA);
		const float invMassB   = GetInverseMass(bodyB);
		const float invMassSum = invMassA + invMassB;
		if (invMassSum <= 0.0f) continue;

		const float invInertiaA = GetInverseInertia(bodyA);
		const float invInertiaB = GetInverseInertia(bodyB);

		const Vector2<float> centerA = GetBodyWorldCenter(scene, manifold.A);
		const Vector2<float> centerB = GetBodyWorldCenter(scene, manifold.B);

		const float restitutionA = bodyA ? bodyA->Restitution : 0.0f;
		const float restitutionB = bodyB ? bodyB->Restitution : 0.0f;

		for (int ci = 0; ci < manifold.ContactCount; ++ci)
		{
			const Vector2<float>& contactPoint = manifold.ContactPoints[ci];
			const Vector2<float>  ra = contactPoint - centerA;
			const Vector2<float>  rb = contactPoint - centerB;

			const float raCrossN  = Cross(ra, normal);
			const float rbCrossN  = Cross(rb, normal);
			const float normalDenom = invMassSum
			    + raCrossN * raCrossN * invInertiaA
			    + rbCrossN * rbCrossN * invInertiaB;
			if (normalDenom <= 0.0f) continue;

			const Vector2<float> vA          = GetContactVelocity(bodyA, centerA, contactPoint);
			const Vector2<float> vB          = GetContactVelocity(bodyB, centerB, contactPoint);
			const float          velAlongN   = Dot(vB - vA, normal);
			if (velAlongN >= 0.0f) continue; // 이미 분리 중 — 건너뜀

			// 저속 충돌에서 반발 없음 (미세 진동 방지)
			const float restitution = (-velAlongN > RESTITUTION_VELOCITY_THRESHOLD)
			                          ? std::max(restitutionA, restitutionB) : 0.0f;

			const float          impulseMag = -(1.0f + restitution) * velAlongN / normalDenom;
			const Vector2<float> impulse    = normal * impulseMag;

			AddImpulseDebug(bodyA, centerA, -impulse, normal, contactPoint, false);
			AddImpulseDebug(bodyB, centerB,  impulse, normal, contactPoint, false);
			ApplyImpulse(bodyA, centerA, -impulse, contactPoint);
			ApplyImpulse(bodyB, centerB,  impulse, contactPoint);

			// ── 마찰 ──────────────────────────────────────────────────────────
			// normal impulse 적용 이후 속도를 재샘플해 정확한 접선 방향 획득
			const Vector2<float> relVelAfter =
			    GetContactVelocity(bodyB, centerB, contactPoint) -
			    GetContactVelocity(bodyA, centerA, contactPoint);
			Vector2<float> tangent = relVelAfter - normal * Dot(relVelAfter, normal);
			if (tangent.LengthSqrt() <= MIN_NORMAL_LENGTH_SQ) continue;

			tangent.Normalize();
			const float raCrossT     = Cross(ra, tangent);
			const float rbCrossT     = Cross(rb, tangent);
			const float tangentDenom = invMassSum
			    + raCrossT * raCrossT * invInertiaA
			    + rbCrossT * rbCrossT * invInertiaB;
			if (tangentDenom <= 0.0f) continue;

			float frictionMag    = -Dot(relVelAfter, tangent) / tangentDenom;
			const float friction = std::sqrt(std::max(0.0f,
			    GetSurfaceFriction(bodyA) * GetSurfaceFriction(bodyB)));
			frictionMag = std::clamp(frictionMag,
			    -impulseMag * friction, impulseMag * friction);

			const Vector2<float> frictionImpulse = tangent * frictionMag;
			AddImpulseDebug(bodyA, centerA, -frictionImpulse, normal, contactPoint, true);
			AddImpulseDebug(bodyB, centerB,  frictionImpulse, normal, contactPoint, true);
			ApplyImpulse(bodyA, centerA, -frictionImpulse, contactPoint);
			ApplyImpulse(bodyB, centerB,  frictionImpulse, contactPoint);
		}
	}
}

// ── 위치 보정 ─────────────────────────────────────────────────────────────────
// 반드시 매니폴드 단위(쌍 단위)로 1회만 적용한다.
// 접촉점 수에 무관하게 침투 보정량은 항상 동일 — 다중 접촉점에서 과보정 없음.

void CPhysics2DSystem::ResolveContactPosition(CScene& scene)
{
	for (const Physics2DManifold& manifold : m_manifolds)
	{
		if (manifold.IsTrigger) continue;

		Transform2D* transformA = scene.GetComponent<Transform2D>(manifold.A);
		Transform2D* transformB = scene.GetComponent<Transform2D>(manifold.B);
		if (!transformA || !transformB) continue;

		Rigidbody2D* bodyA = scene.GetComponent<Rigidbody2D>(manifold.A);
		Rigidbody2D* bodyB = scene.GetComponent<Rigidbody2D>(manifold.B);

		const float invMassA   = GetInverseMass(bodyA);
		const float invMassB   = GetInverseMass(bodyB);
		const float invMassSum = invMassA + invMassB;
		if (invMassSum <= 0.0f) continue;

		const Vector2<float> normal         = NormalizeSafe(manifold.Normal);
		const float          corrDepth      = std::max(manifold.Penetration - POSITION_CORRECTION_SLOP, 0.0f);
		const Vector2<float> correction     = normal * (corrDepth / invMassSum * POSITION_CORRECTION_PERCENT);

		ApplyCorrectionToTransform(scene, manifold.A, *transformA, bodyA, -correction * invMassA);
		ApplyCorrectionToTransform(scene, manifold.B, *transformB, bodyB,  correction * invMassB);
	}
}

// ── 정지 접촉 안정화 ──────────────────────────────────────────────────────────

void CPhysics2DSystem::StabilizeRestingContacts(CScene& scene)
{
	scene.ForEach<Transform2D, Rigidbody2D>([](EntityId, Transform2D&, Rigidbody2D& body)
	{
		if (!body.StabilizeRestingContacts
		    || !body.IsEnabled
		    || EPhysics2DBodyType::Dynamic != body.BodyType
		    || 0 == body.LastContactCount)
		{
			return;
		}

		const float linThresh = std::max(0.0f, body.RestingLinearVelocityThreshold);
		const float angThresh = std::max(0.0f, body.RestingAngularVelocityThreshold);

		if (std::abs(body.AngularVelocity) <= angThresh)
		{
			body.AngularVelocity    = 0.0f;
			body.Torque             = 0.0f;
			body.LastAngularImpulse = 0.0f;
		}
		if (body.Velocity.LengthSqrt() <= linThresh * linThresh)
		{
			body.Velocity = Vector2<float>(0.0f, 0.0f);
			body.Force    = Vector2<float>(0.0f, 0.0f);
		}
	});
}
