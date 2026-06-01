#include "pch.h"
#include "Physics2DSystem.h"

#include "Core/Core.h"
#include "Core/Debug/DebugDraw2D.h"
#include "Core/Logging/LoggerInternal.h"
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
#include <unordered_map>

#if defined(_WIN32)
#include <windows.h>   // OutputDebugStringA — LogTool 안 떠도 VS Output 창에서 확인 가능
#endif

// 충돌 텍스트 진단 — Core::Logger + VS Output 으로 매 fixed step 1회 덤프.
// LogTool 자체에 안 떠도 OutputDebugStringA 로 항상 보장.  0: 비활성(기본).
#ifndef JBRO_PHYSICS_DEBUG_LOG
#define JBRO_PHYSICS_DEBUG_LOG 0
#endif

// 충돌 시각화 — DebugDraw2D 로 매니폴드 normal 화살표 + contact point 그림.
// 씬뷰에서 normal 방향이 잘못 정렬되는지 즉시 확인 가능.  0: 비활성.
#ifndef JBRO_PHYSICS_DEBUG_DRAW
#define JBRO_PHYSICS_DEBUG_DRAW 1
#endif

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
	// 안전 조건(파일 상단 주석): positionIterations × PERCENT < 1.0.
	// positionIterations 기본 2 → PERCENT < 0.5 필수.
	// 0.6 사용 시 누적 보정 = 1.2 → 표면을 통과해 반대 방향으로 분리되는 버그 발생.
	constexpr float POSITION_CORRECTION_PERCENT    = 0.4f;
	constexpr float POSITION_CORRECTION_SLOP       = 0.005f;
	constexpr float RESTITUTION_VELOCITY_THRESHOLD = 1.0f;
	constexpr float MIN_NORMAL_LENGTH_SQ           = 0.000001f;

	// 매니폴드 생성 컷오프 — 이보다 얕은 침투는 매니폴드 자체를 만들지 않음.
	//
	// ResolveContactPosition 은 max(pen - SLOP, 0) 으로 보정량을 계산하므로
	// SLOP 이하 침투는 보정량이 0 인데도 매니폴드는 만들어져 시각화/velocity solver 진입.
	// 그러면 매 sub-step 마다 검출/소멸이 반복되며 화살표가 순간이동하듯 떨린다.
	// SLOP 이하는 "사실상 분리됨" 으로 간주하고 매니폴드 자체 skip.
	// (이전 SLOP*0.5 는 너무 낮아 효과 없었음 — 실제 침투가 SLOP 근처에서 진동.)
	constexpr float MANIFOLD_PENETRATION_CUTOFF    = POSITION_CORRECTION_SLOP;

	// Friction normal impulse floor — resting contact 에서 누적 normalImp 가 매우 작을 때도
	// minimum friction 을 보장한다. 매 sub-step 작은 침투가 반복되며 누적이 못 자라는 환경
	// 보완. 1kg body × gravity(9.8) × subDt(0.005) ≈ 0.05 의 1/10 정도가 적당.
	constexpr float FRICTION_NORMAL_IMPULSE_FLOOR  = 0.005f;

	// Resting tangential velocity threshold — contact 가 있는 body 의 tangent (미끄러짐)
	// 속도가 이 이하이면 강제로 0 처리. 작은 friction + 작은 마찰의 한쪽 누적으로 인한
	// 등속 운동(사용자가 본 "왼쪽으로 일정히 감") 차단. 자유낙하/비접촉 sliding 시에는
	// LastContactCount == 0 이라 영향 없음.
	constexpr float RESTING_TANGENT_VELOCITY       = 0.3f;

	// ── 수학 유틸 ─────────────────────────────────────────────────────────────────

	float Dot(const Vector2& a, const Vector2& b)
	{
		return a.x * b.x + a.y * b.y;
	}

	float Cross(const Vector2& a, const Vector2& b)
	{
		return a.x * b.y - a.y * b.x;
	}

	Vector2 Cross(float scalar, const Vector2& v)
	{
		return Vector2(-scalar * v.y, scalar * v.x);
	}

	// 영벡터에서 NaN을 반환하지 않는 안전한 정규화.
	Vector2 NormalizeSafe(const Vector2& v,
	                             const Vector2& fallback = Vector2(1.0f, 0.0f))
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
	void OrientNormal(Vector2& normal,
	                  const Vector2& centerA,
	                  const Vector2& centerB)
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

	PhysicsAABB2D CalculateAABB(const std::vector<Vector2>& points)
	{
		PhysicsAABB2D aabb;
		if (points.empty())
		{
			return aabb;
		}

		aabb.Min = points[0];
		aabb.Max = points[0];
		for (const Vector2& point : points)
		{
			aabb.Min.x = std::min(aabb.Min.x, point.x);
			aabb.Min.y = std::min(aabb.Min.y, point.y);
			aabb.Max.x = std::max(aabb.Max.x, point.x);
			aabb.Max.y = std::max(aabb.Max.y, point.y);
		}
		return aabb;
	}

	Vector2 CalculateCenter(const std::vector<Vector2>& points)
	{
		Vector2 center(0.0f, 0.0f);
		if (points.empty())
		{
			return center;
		}

		for (const Vector2& point : points)
		{
			center += point;
		}

		return center / static_cast<float>(points.size());
	}

	// 면적 가중 centroid. 산술 평균과 달리 폴리곤 형상에 적합한 reference point.
	// 비대칭/오목 폴리곤에서 OrientNormal 정렬 안정성 확보용.
	// 면적이 0(축퇴) 이면 산술 평균으로 폴백.
	Vector2 CalculateCentroid(const std::vector<Vector2>& points)
	{
		const std::size_t N = points.size();
		if (N < 3)
		{
			return CalculateCenter(points);
		}

		Vector2 c(0.0f, 0.0f);
		float twiceArea = 0.0f;
		for (std::size_t i = 0; i < N; ++i)
		{
			const Vector2& p0 = points[i];
			const Vector2& p1 = points[(i + 1) % N];
			const float cross = p0.x * p1.y - p1.x * p0.y;
			twiceArea += cross;
			c.x += (p0.x + p1.x) * cross;
			c.y += (p0.y + p1.y) * cross;
		}
		if (std::abs(twiceArea) < 1e-6f)
		{
			return CalculateCenter(points);
		}
		return c / (3.0f * twiceArea);
	}

	float CalculateSignedArea(const std::vector<Vector2>& points)
	{
		float area = 0.0f;
		const std::size_t count = points.size();
		for (std::size_t i = 0; i < count; ++i)
		{
			const Vector2& a = points[i];
			const Vector2& b = points[(i + 1) % count];
			area += a.x * b.y - b.x * a.y;
		}
		return area * 0.5f;
	}

	bool IsConvexPolygon(const std::vector<Vector2>& points)
	{
		const std::size_t count = points.size();
		if (count < 4)
		{
			return true;
		}

		float sign = 0.0f;
		for (std::size_t i = 0; i < count; ++i)
		{
			const Vector2& a = points[i];
			const Vector2& b = points[(i + 1) % count];
			const Vector2& c = points[(i + 2) % count];
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

	bool PointInPolygon(const Vector2& point, const std::vector<Vector2>& polygon)
	{
		bool inside = false;
		const std::size_t count = polygon.size();
		for (std::size_t i = 0, j = count - 1; i < count; j = i++)
		{
			const Vector2& a = polygon[i];
			const Vector2& b = polygon[j];
			const bool crosses = ((a.y > point.y) != (b.y > point.y))
				&& (point.x < (b.x - a.x) * (point.y - a.y) / ((b.y - a.y) + 0.0000001f) + a.x);
			if (crosses)
			{
				inside = !inside;
			}
		}
		return inside;
	}

	Vector2 GetPolygonOutwardNormal(const std::vector<Vector2>& polygon, std::size_t edgeIndex)
	{
		const Vector2& a = polygon[edgeIndex];
		const Vector2& b = polygon[(edgeIndex + 1) % polygon.size()];
		const Vector2 edge = b - a;
		const bool isCcw = CalculateSignedArea(polygon) >= 0.0f;
		const Vector2 outward = isCcw
			? Vector2(edge.y, -edge.x)
			: Vector2(-edge.y, edge.x);
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

	Vector2 TransformVector(const Matrix3x2& matrix, const Vector2& vector)
	{
		return Vector2(
			vector.x * matrix.M11 + vector.y * matrix.M21,
			vector.x * matrix.M12 + vector.y * matrix.M22
		);
	}

	Vector2 WorldVectorToParentLocal(const CScene& scene, EntityId entity, const Vector2& worldVector)
	{
		Matrix3x2 parentWorld = CalculateParentWorldTransformNow(scene, entity);
		Matrix3x2 parentWorldInverse;
		if (false == parentWorld.TryInvert(parentWorldInverse))
		{
			return worldVector;
		}
		return TransformVector(parentWorldInverse, worldVector);
	}

	std::uint64_t HashLocalPoints(const std::vector<Vector2>& points)
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
		for (const Vector2& point : points)
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

	Vector2 SupportPoint(const std::vector<Vector2>& points, const Vector2& dir)
	{
		float maxDot = -std::numeric_limits<float>::max();
		Vector2 best = points[0];
		for (const Vector2& p : points)
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

	void ProjectPolygon(const std::vector<Vector2>& points, const Vector2& axis,
	                    float& outMin, float& outMax)
	{
		outMin = Dot(points[0], axis);
		outMax = outMin;
		for (const Vector2& point : points)
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

	bool TestPolygonSAT(const std::vector<Vector2>& a, const std::vector<Vector2>& b,
	                    const std::vector<bool>* aBoundaryEdges, const std::vector<bool>* bBoundaryEdges,
	                    Vector2& outNormal, float& outPenetration)
	{
		// 모든 엣지(대각선 포함)로 분리 축 검사 — 겹침 없으면 즉시 false.
		// 접촉 법선/침투값은 경계(Boundary) 엣지 중 최소 침투 축에서만 선택.
		// 이렇게 해야 내부 대각선이 법선 후보로 잘못 선택되는 것을 막는다.
		float              boundaryMinPen     = std::numeric_limits<float>::max();
		Vector2     boundaryBestNormal = {};
		bool               hasBoundaryAxis    = false;

		for (int shape = 0; shape < 2; ++shape)
		{
			const std::vector<Vector2>& points        = (shape == 0) ? a : b;
			const std::vector<bool>*           boundaryEdges = (shape == 0) ? aBoundaryEdges : bBoundaryEdges;

			for (std::size_t i = 0; i < points.size(); ++i)
			{
				const Vector2& p0        = points[i];
				const Vector2& p1        = points[(i + 1) % points.size()];
				const Vector2  edge      = p1 - p0;
				const Vector2  perpRaw(-edge.y, edge.x);
				const float           perpLenSq = perpRaw.LengthSqrt();
				if (perpLenSq <= MIN_NORMAL_LENGTH_SQ)
				{
					continue; // 길이 0 엣지 — 법선 계산 불가, 건너뜀
				}
				const Vector2 axis = perpRaw * (1.0f / std::sqrt(perpLenSq));

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

	bool TestPolygonSAT(const std::vector<Vector2>& a, const std::vector<Vector2>& b,
	                    Vector2& outNormal, float& outPenetration)
	{
		return TestPolygonSAT(a, b, nullptr, nullptr, outNormal, outPenetration);
	}

	Vector2 ClosestPointOnSegment(const Vector2& point,
	                                     const Vector2& a, const Vector2& b)
	{
		const Vector2 ab       = b - a;
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
		Vector2 Normal = Vector2(1.0f, 0.0f);
		Vector2 Point = Vector2(0.0f, 0.0f);
		float Penetration = 0.0f;
	};

	BoundaryHit2D FindClosestBoundaryHit(const std::vector<Vector2>& polygon,
	                                     const Vector2& point,
	                                     bool pointInside)
	{
		BoundaryHit2D result;
		if (polygon.size() < 2)
		{
			return result;
		}

		float bestDistanceSq = std::numeric_limits<float>::max();
		std::size_t bestEdge = 0;
		Vector2 bestPoint = polygon[0];
		for (std::size_t i = 0; i < polygon.size(); ++i)
		{
			const Vector2& a = polygon[i];
			const Vector2& b = polygon[(i + 1) % polygon.size()];
			const Vector2 closest = ClosestPointOnSegment(point, a, b);
			const float distanceSq = (point - closest).LengthSqrt();
			if (distanceSq < bestDistanceSq)
			{
				bestDistanceSq = distanceSq;
				bestEdge = i;
				bestPoint = closest;
			}
		}

		const float distance = std::sqrt(bestDistanceSq);
		Vector2 normal = pointInside
			? GetPolygonOutwardNormal(polygon, bestEdge)
			: NormalizeSafe(point - bestPoint, GetPolygonOutwardNormal(polygon, bestEdge));

		const Vector2 outward = GetPolygonOutwardNormal(polygon, bestEdge);
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

	bool TrySegmentIntersection(const Vector2& a0, const Vector2& a1,
	                            const Vector2& b0, const Vector2& b1,
	                            Vector2& outPoint)
	{
		const Vector2 r = a1 - a0;
		const Vector2 s = b1 - b0;
		const float denom = Cross(r, s);
		if (std::abs(denom) <= MIN_NORMAL_LENGTH_SQ)
		{
			return false;
		}

		const Vector2 diff = b0 - a0;
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
	                               Vector2& outNormal, Vector2& outContactPoint,
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

	bool TestBoundaryPolygonPolygonOneWay(const std::vector<Vector2>& boundaryPolygon,
	                                      const std::vector<Vector2>& testPolygon,
	                                      Vector2& outNormal,
	                                      Vector2& outContactPoint,
	                                      float& outPenetration)
	{
		bool hitAny = false;
		float bestPenetration = -std::numeric_limits<float>::max();
		Vector2 bestNormal(1.0f, 0.0f);
		Vector2 bestPoint(0.0f, 0.0f);

		for (const Vector2& point : testPolygon)
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
				const Vector2& ba = boundaryPolygon[bi];
				const Vector2& bb = boundaryPolygon[(bi + 1) % boundaryPolygon.size()];
				for (std::size_t ti = 0; ti < testPolygon.size(); ++ti)
				{
					const Vector2& ta = testPolygon[ti];
					const Vector2& tb = testPolygon[(ti + 1) % testPolygon.size()];
					Vector2 intersection;
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
	                                Vector2& outNormal,
	                                Vector2& outContactPoint,
	                                float& outPenetration)
	{
		Vector2 nAB;
		Vector2 pAB;
		float penAB = 0.0f;
		const bool hitAB = TestBoundaryPolygonPolygonOneWay(a.WorldPoints, b.WorldPoints, nAB, pAB, penAB);

		Vector2 nBA;
		Vector2 pBA;
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
	                      Vector2& outNormal, Vector2& outContactPoint,
	                      float& outPenetration)
	{
		const Vector2 delta      = b.WorldCenter - a.WorldCenter;
		const float          distanceSq = delta.LengthSqrt();
		const float          radiusSum  = a.WorldRadius + b.WorldRadius;
		if (distanceSq > radiusSum * radiusSum)
		{
			return false;
		}

		const float distance = std::sqrt(distanceSq);
		outNormal       = (distance > 0.0f) ? delta / distance : Vector2(1.0f, 0.0f);
		outContactPoint = a.WorldCenter + outNormal * (a.WorldRadius - (radiusSum - distance) * 0.5f);
		outPenetration  = radiusSum - distance;
		return true;
	}

	// 볼록 폴리곤 점 목록과 원의 충돌 (SAT).
	// PolygonCollider2D 전체가 아닌 단일 볼록 점 집합을 받는 버전.
	bool TestConvexPointsCircle(const std::vector<Vector2>& polyPts,
	                            const std::vector<bool>* boundaryEdges,
	                            const CircleCollider2D& circle,
	                            Vector2& outNormal, float& outPenetration)
	{
		if (polyPts.size() < 3) return false;

		outPenetration = std::numeric_limits<float>::max();
		bool bestAxisIsBoundary = true;

		Vector2 closestPt  = polyPts[0];
		float          closestDSq = std::numeric_limits<float>::max();

		for (std::size_t i = 0; i < polyPts.size(); ++i)
		{
			const Vector2& p0 = polyPts[i];
			const Vector2& p1 = polyPts[(i + 1) % polyPts.size()];

			const Vector2 segPt = ClosestPointOnSegment(circle.WorldCenter, p0, p1);
			const float          dSq   = (circle.WorldCenter - segPt).LengthSqrt();
			if (dSq < closestDSq) { closestDSq = dSq; closestPt = segPt; }

			const Vector2 edge      = p1 - p0;
			const Vector2 perpRaw(-edge.y, edge.x);
			const float          perpLenSq = perpRaw.LengthSqrt();
			if (perpLenSq <= MIN_NORMAL_LENGTH_SQ) continue;

			const Vector2 axis = perpRaw * (1.0f / std::sqrt(perpLenSq));
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

		const Vector2 vAxisRaw = circle.WorldCenter - closestPt;
		if (vAxisRaw.LengthSqrt() > MIN_NORMAL_LENGTH_SQ)
		{
			const Vector2 vAxis = NormalizeSafe(vAxisRaw);
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
	                       Vector2& outNormal, float& outPenetration)
	{
		if (polygon.WorldPoints.size() < 3)
		{
			return false;
		}

		outPenetration = std::numeric_limits<float>::max();

		Vector2 closestPolygonPoint = polygon.WorldPoints[0];
		float          closestDistanceSq   = std::numeric_limits<float>::max();

		for (std::size_t i = 0; i < polygon.WorldPoints.size(); ++i)
		{
			const Vector2& p0 = polygon.WorldPoints[i];
			const Vector2& p1 = polygon.WorldPoints[(i + 1) % polygon.WorldPoints.size()];

			const Vector2 segPoint = ClosestPointOnSegment(circle.WorldCenter, p0, p1);
			const float          segDistSq = (circle.WorldCenter - segPoint).LengthSqrt();
			if (segDistSq < closestDistanceSq)
			{
				closestDistanceSq   = segDistSq;
				closestPolygonPoint = segPoint;
			}

			const Vector2 edge      = p1 - p0;
			const Vector2 perpRaw(-edge.y, edge.x);
			const float          perpLenSq = perpRaw.LengthSqrt();
			if (perpLenSq <= MIN_NORMAL_LENGTH_SQ)
			{
				continue;
			}
			const Vector2 axis = perpRaw * (1.0f / std::sqrt(perpLenSq));

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
		const Vector2 vertexAxisRaw = circle.WorldCenter - closestPolygonPoint;
		if (vertexAxisRaw.LengthSqrt() > MIN_NORMAL_LENGTH_SQ)
		{
			const Vector2 vertexAxis = NormalizeSafe(vertexAxisRaw);
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

	float CalculatePolygonInertia(const std::vector<Vector2>& localPoints, float mass)
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
			const Vector2& p0    = localPoints[i];
			const Vector2& p1    = localPoints[(i + 1) % N];
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
		Vector2 worldDelta = body.Velocity * dt;
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
	                                const Vector2& correction)
	{
		Vector2 worldDelta = correction;
		if (body)
		{
			if (body->FreezePositionX) worldDelta.x = 0.0f;
			if (body->FreezePositionY) worldDelta.y = 0.0f;
		}
		transform.Position += WorldVectorToParentLocal(scene, entity, worldDelta);
	}

	// Body 의 회전 중심(center of mass 근사) 을 반환.
	//
	// ★ 비대칭 폴리곤에서 entity origin 을 회전 중심으로 쓰면 Cross(ra, impulse) 가
	//   잘못된 토크를 만들어 angular velocity 가 누적된다 → linear velocity 도 같이 폭주.
	// 콜라이더가 있으면 그 면적 가중 centroid 를 사용 (질량 균등 분포 가정).
	// 콜라이더가 없으면 entity origin 폴백 (회전 안 함 / kinematic 케이스).
	Vector2 GetBodyWorldCenter(const CScene& scene, EntityId entity)
	{
		if (const PolygonCollider2D* poly = scene.GetComponent<PolygonCollider2D>(entity);
			poly && poly->IsEnabled && poly->WorldPoints.size() >= 3)
		{
			return CalculateCentroid(poly->WorldPoints);
		}
		if (const CircleCollider2D* circle = scene.GetComponent<CircleCollider2D>(entity);
			circle && circle->IsEnabled)
		{
			return circle->WorldCenter;
		}
		const Matrix3x2 worldTransform = CalculateWorldTransformNow(scene, entity);
		return worldTransform.TransformPoint(Vector2(0.0f, 0.0f));
	}

	Vector2 GetContactVelocity(const Rigidbody2D* body,
	                                  const Vector2& bodyCenter,
	                                  const Vector2& contactPoint)
	{
		if (nullptr == body)
		{
			return Vector2(0.0f, 0.0f);
		}
		return body->Velocity + Cross(body->AngularVelocity, contactPoint - bodyCenter);
	}

	void AddImpulseDebug(Rigidbody2D* body, const Vector2& bodyCenter,
	                     const Vector2& impulse,
	                     const Vector2& contactNormal,
	                     const Vector2& contactPoint,
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

	void ApplyImpulse(Rigidbody2D* body, const Vector2& bodyCenter,
	                  const Vector2& impulse, const Vector2& contactPoint)
	{
		if (nullptr == body || !body->IsEnabled || EPhysics2DBodyType::Dynamic != body->BodyType)
		{
			return;
		}

		const float inverseMass = GetInverseMass(body);
		if (inverseMass > 0.0f)
		{
			Vector2 linear = impulse * inverseMass;
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
		Vector2 Points[2];
		std::uint32_t  FeatureIds[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };
		int            Count = 0;
	};

	// Polygon-vs-polygon FeatureId 인코딩 — reference face / incident face / contact 순서.
	// 같은 두 face 조합에서 검출된 contact 는 같은 ID 를 받아 prev 매니폴드와 매칭 가능.
	constexpr std::uint32_t MakePolyFeatureId(int refIdx, int incIdx, int contactOrder)
	{
		return (static_cast<std::uint32_t>(refIdx) << 16)
		     | (static_cast<std::uint32_t>(incIdx) << 4)
		     |  static_cast<std::uint32_t>(contactOrder & 0xF);
	}

	int ClipSegment(const Vector2& p0, const Vector2& p1,
	                const Vector2& planeNormal, float planeOffset,
	                Vector2 out[2])
	{
		const float d0 = Dot(p0, planeNormal) - planeOffset;
		const float d1 = Dot(p1, planeNormal) - planeOffset;
		int k = 0;
		if (d0 >= 0.0f)                   out[k++] = p0;
		if ((d0 < 0.0f) != (d1 < 0.0f))  out[k++] = p0 + (p1 - p0) * (d0 / (d0 - d1));
		if (d1 >= 0.0f && k < 2)          out[k++] = p1;
		return k;
	}

	Vector2 GetFaceOutwardNormal(const std::vector<Vector2>& poly,
	                                    const Vector2& polyCenter, int i)
	{
		const int            N     = static_cast<int>(poly.size());
		const Vector2 edge  = poly[(i + 1) % N] - poly[i];
		const Vector2 leftN(-edge.y, edge.x);
		const Vector2 mid  = (poly[i] + poly[(i + 1) % N]) * 0.5f;
		return (Dot(leftN, mid - polyCenter) >= 0.0f) ? leftN : -leftN;
	}

	ContactManifold2D BuildPolygonManifold(const std::vector<Vector2>& a,
	                                       const std::vector<Vector2>& b,
	                                       const Vector2& normal)
	{
		const int NA = static_cast<int>(a.size());
		const int NB = static_cast<int>(b.size());

		// fallback: A에서 normal 방향 지지점, B에서 -normal 방향 지지점의 중점 (7순위 수정)
		auto Fallback = [&]() -> ContactManifold2D
		{
			ContactManifold2D m;
			m.Points[0]     = (SupportPoint(a, normal) + SupportPoint(b, -normal)) * 0.5f;
			m.FeatureIds[0] = 0xFFFFFFFFu; // fallback contact 는 매칭 불가
			m.Count         = 1;
			return m;
		};

		if (NA < 2 || NB < 2) return Fallback();

		const Vector2 centerA = CalculateCenter(a);
		const Vector2 centerB = CalculateCenter(b);

		// 1. A에서 reference face 선택 (normal과 가장 정렬된 면)
		float bestDot = -std::numeric_limits<float>::max();
		int   refIdx  = 0;
		for (int i = 0; i < NA; ++i)
		{
			const float d = Dot(GetFaceOutwardNormal(a, centerA, i), normal);
			if (d > bestDot) { bestDot = d; refIdx = i; }
		}
		const Vector2& rv0    = a[refIdx];
		const Vector2& rv1    = a[(refIdx + 1) % NA];
		const Vector2  rEdge  = rv1 - rv0;
		const float           rLenSq = rEdge.LengthSqrt();
		if (rLenSq <= MIN_NORMAL_LENGTH_SQ) return Fallback();
		const Vector2 rDir = rEdge * (1.0f / std::sqrt(rLenSq));

		// 2. B에서 incident face 선택 (normal과 가장 반대인 면)
		float worstDot = std::numeric_limits<float>::max();
		int   incIdx   = 0;
		for (int i = 0; i < NB; ++i)
		{
			const float d = Dot(GetFaceOutwardNormal(b, centerB, i), normal);
			if (d < worstDot) { worstDot = d; incIdx = i; }
		}
		Vector2 seg[2] = { b[incIdx], b[(incIdx + 1) % NB] };
		int segCount = 2;

		// 3. 두 측면 평면으로 incident 선분 클리핑
		{
			Vector2 tmp[2];
			const int cnt = ClipSegment(seg[0], seg[1], rDir, Dot(rDir, rv0), tmp);
			segCount = cnt;
			if (cnt > 0) seg[0] = tmp[0];
			if (cnt > 1) seg[1] = tmp[1];
		}
		if (segCount == 0) return Fallback();

		{
			Vector2 tmp[2];
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
		const Vector2 rNormRaw = GetFaceOutwardNormal(a, centerA, refIdx);
		const float          rNormLen = std::sqrt(rNormRaw.LengthSqrt());
		const Vector2 rNorm    = (rNormLen > 1e-6f) ? rNormRaw * (1.0f / rNormLen) : normal;
		const float          refOffset = Dot(rNorm, rv0);

		ContactManifold2D manifold;
		for (int i = 0; i < segCount; ++i)
		{
			if (Dot(rNorm, seg[i]) - refOffset <= 0.02f && manifold.Count < 2)
			{
				manifold.Points[manifold.Count]     = seg[i];
				// (refIdx, incIdx, segment ordinal) 로 contact 식별.
				// 두 폴리곤이 같은 face 조합 + 같은 순서 contact 면 다음 step 에서 같은 id.
				manifold.FeatureIds[manifold.Count] = MakePolyFeatureId(refIdx, incIdx, i);
				manifold.Count++;
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
	float SignedArea(const std::vector<Vector2>& pts, const std::vector<int>& idx)
	{
		float area = 0.0f;
		const int n = static_cast<int>(idx.size());
		for (int i = 0; i < n; ++i)
		{
			const Vector2& p0 = pts[idx[i]];
			const Vector2& p1 = pts[idx[(i + 1) % n]];
			area += p0.x * p1.y - p1.x * p0.y;
		}
		return area * 0.5f;
	}

	// 점 P가 삼각형 ABC 안에 있는지 (경계 포함)
	bool PointInTriangle(const Vector2& p,
	                     const Vector2& a,
	                     const Vector2& b,
	                     const Vector2& c)
	{
		const float d1 = Cross(b - a, p - a);
		const float d2 = Cross(c - b, p - b);
		const float d3 = Cross(a - c, p - c);
		const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
		const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
		return !(hasNeg && hasPos);
	}

	bool PointsEqual(const Vector2& a, const Vector2& b)
	{
		constexpr float POINT_EPSILON = 0.0001f;
		return std::abs(a.x - b.x) <= POINT_EPSILON
			&& std::abs(a.y - b.y) <= POINT_EPSILON;
	}

	bool IsOriginalPolygonEdge(const std::vector<Vector2>& polygon,
	                           const Vector2& a, const Vector2& b)
	{
		if (polygon.size() < 2)
		{
			return false;
		}

		for (std::size_t i = 0; i < polygon.size(); ++i)
		{
			const Vector2& p0 = polygon[i];
			const Vector2& p1 = polygon[(i + 1) % polygon.size()];
			if ((PointsEqual(a, p0) && PointsEqual(b, p1))
			    || (PointsEqual(a, p1) && PointsEqual(b, p0)))
			{
				return true;
			}
		}
		return false;
	}

	void TriangulateEarClip(
		const std::vector<Vector2>& pts,
		std::vector<std::array<Vector2, 3>>& outTriangles)
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

				const Vector2& A = pts[idx[prev]];
				const Vector2& B = pts[idx[i]];
				const Vector2& C = pts[idx[next]];

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

		std::vector<std::array<Vector2, 3>> tris;
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

void CPhysics2DSystem::SetGravity(const Vector2& gravity)             { m_gravity = gravity; }
const Vector2& CPhysics2DSystem::GetGravity() const                  { return m_gravity; }

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
	const float subDt = fixedDelta / static_cast<float>(numSubSteps);
	for (int sub = 0; sub < numSubSteps; ++sub)
	{
		Step(scene, subDt);
	}

	// 콜라이더 월드 좌표만 최종 동기화 (다음 frame 쿼리/렌더용).
	// 매니폴드는 마지막 sub-step 결과 그대로 둠 — AccumulatedImpulse 가 다음 frame 의
	// MatchAndWarmStart 로 전달되어야 함. 통합 detect 호출 시 AccumulatedImpulse 가
	// 0 으로 리셋되어 warm-start 메커니즘 자체가 무력화되므로 제거.
	UpdateColliderBounds(scene);

#if JBRO_PHYSICS_DEBUG_DRAW
	// 마지막 Step 의 마지막 DetectContacts 결과 = 다음 프레임 시작 시점의 매니폴드.
	// 이 한 상태만 시각화하여 화살표가 sub-step 사이에 진동하지 않음.
	DrawManifoldDebugLines();
#endif

#if JBRO_PHYSICS_DEBUG_LOG
	// ── 동적 body 의 fixed step 단위 velocity 변화 추적 ────────────────────────
	// 마찰/iteration 무관하게 한 방향 velocity 가 누적되면 매니폴드 normal 자체가
	// 한쪽으로 편향되어 매 sub-step 새 impulse 가 적용되는 contact persistence 부재 케이스.
	// dv 와 LastContactNormal 의 부호 일치를 보면 normal impulse 누적 여부 확인 가능.
	{
		static int s_bodyLogCnt = 0;
		static std::unordered_map<EntityId, Vector2> s_prevVel;
		if (++s_bodyLogCnt >= 60)
		{
			s_bodyLogCnt = 0;
			scene.ForEach<Rigidbody2D>([&](EntityId e, Rigidbody2D& body)
				{
					if (false == body.IsEnabled || EPhysics2DBodyType::Dynamic != body.BodyType) return;

					const auto it = s_prevVel.find(e);
					const Vector2 prev = (it != s_prevVel.end()) ? it->second : Vector2(0.0f, 0.0f);
					const Vector2 dv = body.Velocity - prev;
					s_prevVel[e] = body.Velocity;

					// velocity 또는 dv 의 절대값이 의미 있는 크기일 때만 로그 (조용한 body 무시).
					if (body.Velocity.LengthSqrt() < 0.01f && dv.LengthSqrt() < 0.001f) return;

					const std::string line = std::format(
						"[Phys-Body] id={} v=({:+.3f},{:+.3f}) dv=({:+.4f},{:+.4f}) "
						"contacts={} normImp={:.3f} fricImp={:.3f} angVel={:+.3f} "
						"lastN=({:+.2f},{:+.2f})",
						static_cast<unsigned long long>(e),
						body.Velocity.x, body.Velocity.y,
						dv.x, dv.y,
						body.LastContactCount,
						body.LastNormalImpulse,
						body.LastFrictionImpulse,
						body.AngularVelocity,
						body.LastContactNormal.x, body.LastContactNormal.y);
					CSystemLog::Info(line);
#if defined(_WIN32)
					OutputDebugStringA((line + "\n").c_str());
#endif
				});
		}
	}
#endif
}

void CPhysics2DSystem::DrawManifoldDebugLines()
{
	if (false == Core::DebugDraw2D.IsValid())
	{
		return;
	}

	IDebugDraw2D& dd = *Core::DebugDraw2D;
	constexpr DebugColor kPointCol  = DebugColorRGBA(255, 230,  60, 255);  // 노랑 contact point
	constexpr DebugColor kNormalCol = DebugColorRGBA(255,  60, 220, 255);  // 마젠타 normal
	constexpr DebugColor kHeadCol   = DebugColorRGBA(255, 100, 255, 255);

	for (const Physics2DManifold& m : m_manifolds)
	{
		const float arrowLen = std::max(m.Penetration * 4.0f, 0.25f);
		for (int ci = 0; ci < m.ContactCount; ++ci)
		{
			const Vector2& cp  = m.ContactPoints[ci];
			const Vector2  tip = cp + m.Normal * arrowLen;

			dd.DrawCircle(cp, 0.04f, kPointCol, 1.5f, 12);
			dd.DrawLine(cp, tip, kNormalCol, 1.5f);

			// 화살표 머리 (V 모양)
			const Vector2 back = -m.Normal * (arrowLen * 0.25f);
			const Vector2 perp(-m.Normal.y, m.Normal.x);
			const Vector2 headL = tip + back + perp * (arrowLen * 0.15f);
			const Vector2 headR = tip + back - perp * (arrowLen * 0.15f);
			dd.DrawLine(tip, headL, kHeadCol, 1.5f);
			dd.DrawLine(tip, headR, kHeadCol, 1.5f);
		}
	}
}

void CPhysics2DSystem::Step(CScene& scene, float deltaSeconds)
{
	// 직전 sub-step 의 매니폴드를 prev 로 보존 — contact persistence 매칭에 사용.
	m_prevManifolds.swap(m_manifolds);
	m_manifolds.clear();

	IntegrateBodies(scene, deltaSeconds);
	UpdateColliderBounds(scene);
	DetectContacts(scene);

	// prev 매니폴드와 매칭 → 누적 impulse 복원 + body 에 warm-start impulse 적용.
	MatchAndWarmStart(scene);

	for (int i = 0; i < m_velocityIterations; ++i)
	{
		ResolveContactVelocity(scene);
	}

	for (int i = 0; i < m_positionIterations; ++i)
	{
		ResolveContactPosition(scene);
	}

	StabilizeRestingContacts(scene);
}

// ── 적분 ──────────────────────────────────────────────────────────────────────

void CPhysics2DSystem::IntegrateBodies(CScene& scene, float deltaSeconds)
{
	scene.ForEach<Transform2D, Rigidbody2D>([this, &scene, deltaSeconds](EntityId entity, Transform2D& transform, Rigidbody2D& body)
	{
		body.LastContactCount    = 0;
		body.LastContactNormal   = Vector2(0.0f, 0.0f);
		body.LastContactPoint    = Vector2(0.0f, 0.0f);
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

		// ── 각속도 클램프 — 회전 폭주 방지 ───────────────────────────────────────
		// 비대칭 폴리곤 + 잘못된 회전 중심이 만든 angular impulse 누적이 spin out 으로
		// 이어지는 것을 차단. Box2D b2_maxRotation 과 유사한 안전망.
		// 4π/s (=2회전/초) 가 일반 게임 캐릭터 기준 충분히 큰 상한.
		{
			constexpr float MAX_ANGULAR_VELOCITY = 4.0f * 3.14159265f;
			if (body.AngularVelocity >  MAX_ANGULAR_VELOCITY) body.AngularVelocity =  MAX_ANGULAR_VELOCITY;
			if (body.AngularVelocity < -MAX_ANGULAR_VELOCITY) body.AngularVelocity = -MAX_ANGULAR_VELOCITY;
		}

		ApplyVelocityToTransform(scene, entity, transform, body, deltaSeconds);
		body.Force  = Vector2(0.0f, 0.0f);
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
		for (const Vector2& p : collider.LocalPoints)
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
		collider.WorldCenter  = wt.TransformPoint(Vector2(0.0f, 0.0f));
		const float scaleX    = std::sqrt(wt.M11 * wt.M11 + wt.M12 * wt.M12);
		const float scaleY    = std::sqrt(wt.M21 * wt.M21 + wt.M22 * wt.M22);
		collider.WorldRadius  = collider.Radius * std::max(scaleX, scaleY);
		const float r         = collider.WorldRadius;
		collider.WorldAABB.Min = Vector2(collider.WorldCenter.x - r, collider.WorldCenter.y - r);
		collider.WorldAABB.Max = Vector2(collider.WorldCenter.x + r, collider.WorldCenter.y + r);
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

			Vector2 bestNormal;
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

							Vector2 n; float pen = 0.0f;
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

							Vector2 n; float pen = 0.0f;
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

								Vector2 n; float pen = 0.0f;
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
			// 얕은 침투 컷오프 — 시각적 떨림 + 미세 매니폴드 폭주 방지
			if (bestPen <= MANIFOLD_PENETRATION_CUTOFF) continue;

			// 면적 가중 centroid — 비대칭/오목 폴리곤에서 산술 평균은 reference 부적합.
			const Vector2 centerA = CalculateCentroid(a->WorldPoints);
			const Vector2 centerB = CalculateCentroid(b->WorldPoints);
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
			{
				manifold.ContactPoints[mi] = cm.Points[mi];
				manifold.FeatureIds[mi]    = cm.FeatureIds[mi];
			}

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

			Vector2 normal, contactPoint;
			float          penetration;
			if (!TestCircleCircle(*a, *b, normal, contactPoint, penetration)) continue;
			// 얕은 침투 컷오프 — 시각적 떨림 + 미세 매니폴드 폭주 방지
			if (penetration <= MANIFOLD_PENETRATION_CUTOFF) continue;

			// TestCircleCircle이 반환한 normal은 이미 A→B 방향이지만 일관성을 위해 호출
			OrientNormal(normal, a->WorldCenter, b->WorldCenter);

			Physics2DManifold manifold;
			manifold.A                = ea;
			manifold.B                = eb;
			manifold.Normal           = normal;
			manifold.Penetration      = penetration;
			manifold.IsTrigger        = a->IsTrigger || b->IsTrigger;
			manifold.ContactPoints[0] = contactPoint;
			manifold.FeatureIds[0]    = 0;   // circle-circle 은 contact 1개로 항상 동일 id
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
			Vector2 bestNormal;
			Vector2 bestContactPoint;

			if (IsConvexPolygon(polygon->WorldPoints))
			{
				// 볼록 폴리곤: 전체 WorldPoints 직접 SAT
				Vector2 n; float pen;
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

					Vector2 n; float pen;
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
					Vector2 n; float pen;
					if (TestPolygonCircle(*polygon, *circle, n, pen))
					{
						bestPen = pen; bestNormal = n; bestHit = true;
					}
				}
			}

			if (!bestHit) continue;
			// 얕은 침투 컷오프 — 시각적 떨림 + 미세 매니폴드 폭주 방지
			if (bestPen <= MANIFOLD_PENETRATION_CUTOFF) continue;

			// 면적 가중 centroid — 비대칭/오목 폴리곤 정렬 안정성.
			const Vector2 centerA = CalculateCentroid(polygon->WorldPoints);
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
			manifold.FeatureIds[0]    = 0;   // polygon-circle 도 contact 1개로 항상 동일 id
			manifold.ContactCount     = 1;

			m_manifolds.push_back(manifold);
		}
	}

	// ── 같은 (A,B) 페어 매니폴드 병합 ─────────────────────────────────────────
	// 한 entity 쌍이 polygon-vs-polygon 검출 + polygon-vs-circle 검출 등으로 매니폴드
	// 여러 개 생성되면 normal/penetration 이 모순될 수 있다. iteration 간 침투 재계산을
	// 하지 않으므로 ResolveContactPosition 이 모순된 normal 을 합산해 잘못된 방향 분리.
	// 같은 페어를 하나로 병합 — 가장 깊은 침투 매니폴드를 base 로, 나머지의 contact point 만
	// 추가(최대 2개). normal 안정성을 위해 base 의 normal 그대로 사용.
	if (m_manifolds.size() > 1)
	{
		std::unordered_map<std::uint64_t, std::size_t> firstIndexOfPair;
		std::vector<Physics2DManifold> merged;
		merged.reserve(m_manifolds.size());

		for (const Physics2DManifold& m : m_manifolds)
		{
			const std::uint64_t lo  = std::min(static_cast<std::uint64_t>(m.A), static_cast<std::uint64_t>(m.B));
			const std::uint64_t hi  = std::max(static_cast<std::uint64_t>(m.A), static_cast<std::uint64_t>(m.B));
			const std::uint64_t key = (lo << 32) | hi;

			auto it = firstIndexOfPair.find(key);
			if (it == firstIndexOfPair.end())
			{
				firstIndexOfPair[key] = merged.size();
				merged.push_back(m);
				continue;
			}

			Physics2DManifold& dst = merged[it->second];

			// 더 깊은 침투 매니폴드의 normal/penetration 으로 교체.
			// 이렇게 해야 normal 이 max-pen 쪽으로 안정적으로 정렬.
			if (m.Penetration > dst.Penetration)
			{
				// 이전 dst 의 contact 들을 보존 (FeatureId/누적 impulse 포함).
				const int      savedCount       = dst.ContactCount;
				Vector2 savedPts[2]      = { dst.ContactPoints[0], dst.ContactPoints[1] };
				std::uint32_t  savedFeat[2]     = { dst.FeatureIds[0], dst.FeatureIds[1] };
				float          savedAccN[2]     = { dst.AccumulatedNormalImpulse[0],   dst.AccumulatedNormalImpulse[1]   };
				float          savedAccF[2]     = { dst.AccumulatedFrictionImpulse[0], dst.AccumulatedFrictionImpulse[1] };

				dst = m;

				for (int i = 0; i < savedCount && dst.ContactCount < 2; ++i)
				{
					const int k = dst.ContactCount;
					dst.ContactPoints[k]              = savedPts[i];
					dst.FeatureIds[k]                 = savedFeat[i];
					dst.AccumulatedNormalImpulse[k]   = savedAccN[i];
					dst.AccumulatedFrictionImpulse[k] = savedAccF[i];
					++dst.ContactCount;
				}
			}
			else
			{
				// m 의 contact 들을 dst 뒤에 추가 (최대 2개).
				for (int i = 0; i < m.ContactCount && dst.ContactCount < 2; ++i)
				{
					const int k = dst.ContactCount;
					dst.ContactPoints[k]              = m.ContactPoints[i];
					dst.FeatureIds[k]                 = m.FeatureIds[i];
					dst.AccumulatedNormalImpulse[k]   = m.AccumulatedNormalImpulse[i];
					dst.AccumulatedFrictionImpulse[k] = m.AccumulatedFrictionImpulse[i];
					++dst.ContactCount;
				}
			}

			// 트리거 플래그 OR 합성.
			dst.IsTrigger = dst.IsTrigger || m.IsTrigger;
		}

		m_manifolds = std::move(merged);
	}

#if JBRO_PHYSICS_DEBUG_LOG
	// ── 텍스트 진단: CSystemLog (LogTool) + OutputDebugStringA (VS Output) 동시 출력 ──
	// Engine.Debug SafePtr 가 invalid 한 케이스에도 무조건 출력되도록 raw 경로 사용.
	// FixedUpdate 빈도 × sub-step × 2(detect 호출) 라 매번 찍으면 폭주 → 1초당 1회로 throttle.
	{
		static int s_logFrameCounter = 0;
		if (++s_logFrameCounter >= 60)
		{
			s_logFrameCounter = 0;

			if (m_manifolds.empty())
			{
				constexpr const char* emptyMsg = "[Physics] no manifolds this frame";
				CSystemLog::Info(emptyMsg);
		#if defined(_WIN32)
				OutputDebugStringA("[Physics] no manifolds this frame\n");
		#endif
			}

			std::unordered_map<std::uint64_t, int> pairCount;
			for (const Physics2DManifold& m : m_manifolds)
			{
				const std::uint64_t lo  = std::min(static_cast<std::uint64_t>(m.A), static_cast<std::uint64_t>(m.B));
				const std::uint64_t hi  = std::max(static_cast<std::uint64_t>(m.A), static_cast<std::uint64_t>(m.B));
				pairCount[(lo << 32) | hi]++;
			}

			for (const Physics2DManifold& m : m_manifolds)
			{
				const std::string line = std::format(
					"[Physics] A={} B={} n=({:.3f},{:.3f}) pen={:.4f} ctc={} trigger={}",
					static_cast<unsigned long long>(m.A),
					static_cast<unsigned long long>(m.B),
					m.Normal.x, m.Normal.y,
					m.Penetration, m.ContactCount,
					m.IsTrigger ? 1 : 0);
				CSystemLog::Info(line);
		#if defined(_WIN32)
				OutputDebugStringA((line + "\n").c_str());
		#endif
			}
			for (const auto& [key, count] : pairCount)
			{
				if (count > 1)
				{
					const std::uint32_t a = static_cast<std::uint32_t>(key >> 32);
					const std::uint32_t b = static_cast<std::uint32_t>(key & 0xFFFFFFFFu);
					const std::string warn = std::format(
						"[Physics] duplicate manifolds: pair=({},{}) count={}", a, b, count);
					CSystemLog::Warning(warn);
		#if defined(_WIN32)
					OutputDebugStringA((warn + "\n").c_str());
		#endif
				}
			}
		}
	}
#endif

	// 디버그 시각화는 OnFixedUpdate 끝에서 한 번만 호출 (DrawManifoldDebugLines).
	// DetectContacts 는 한 fixed step 안에서 sub-step × 2 회 호출되므로 매번 그리면
	// 같은 매니폴드가 여러 번 덮어쓰여져 화살표가 시각적으로 떨린다.
}

// ── Contact persistence: 직전 step 매니폴드와 매칭 + warm-start ──────────────
// 같은 (A,B) 페어 + 같은 FeatureId 인 contact 가 직전 step 에도 있었으면,
// 그 contact 의 누적 normal/friction impulse 를 복원하고 한 번 body 에 적용 (warm-start).
// 효과:
//   - friction clamp 가 매 step 새로 시작되지 않고 누적값 기준으로 동작 → friction 정상
//   - 정지 접촉이 빠르게 수렴 (warm-start 가 거의 정답에 가까운 초기값 제공)
//   - velocity solver iteration 수가 적어도 수렴
void CPhysics2DSystem::MatchAndWarmStart(CScene& scene)
{
	if (m_prevManifolds.empty() || m_manifolds.empty())
	{
		return;
	}

	// 직전 매니폴드를 (페어, contactIndex) → (normalImp, frictionImp) 룩업 테이블로 변환.
	// FeatureId 가 0xFFFFFFFFu(매칭 불가) 인 contact 는 skip.
	struct PrevImpulse { float Norm; float Fric; };
	struct PrevKey
	{
		std::uint64_t Pair;       // (lo<<32) | hi
		std::uint32_t FeatureId;
		bool operator==(const PrevKey& o) const { return Pair == o.Pair && FeatureId == o.FeatureId; }
	};
	struct PrevKeyHash
	{
		std::size_t operator()(const PrevKey& k) const noexcept
		{
			return std::hash<std::uint64_t>{}(k.Pair) ^ (std::hash<std::uint32_t>{}(k.FeatureId) << 1);
		}
	};

	std::unordered_map<PrevKey, PrevImpulse, PrevKeyHash> prevTable;
	prevTable.reserve(m_prevManifolds.size() * 2);

	for (const Physics2DManifold& pm : m_prevManifolds)
	{
		const std::uint64_t lo  = std::min(static_cast<std::uint64_t>(pm.A), static_cast<std::uint64_t>(pm.B));
		const std::uint64_t hi  = std::max(static_cast<std::uint64_t>(pm.A), static_cast<std::uint64_t>(pm.B));
		const std::uint64_t key = (lo << 32) | hi;
		for (int i = 0; i < pm.ContactCount; ++i)
		{
			if (pm.FeatureIds[i] == 0xFFFFFFFFu) continue;
			prevTable[PrevKey{ key, pm.FeatureIds[i] }] = { pm.AccumulatedNormalImpulse[i], pm.AccumulatedFrictionImpulse[i] };
		}
	}

	// 새 매니폴드와 매칭 + warm-start impulse 적용.
	for (Physics2DManifold& m : m_manifolds)
	{
		if (m.IsTrigger || m.ContactCount <= 0) continue;

		const std::uint64_t lo  = std::min(static_cast<std::uint64_t>(m.A), static_cast<std::uint64_t>(m.B));
		const std::uint64_t hi  = std::max(static_cast<std::uint64_t>(m.A), static_cast<std::uint64_t>(m.B));
		const std::uint64_t key = (lo << 32) | hi;

		Rigidbody2D* bodyA = scene.GetComponent<Rigidbody2D>(m.A);
		Rigidbody2D* bodyB = scene.GetComponent<Rigidbody2D>(m.B);
		const Vector2 normal  = NormalizeSafe(m.Normal);
		const Vector2 tangent(-normal.y, normal.x);
		const Vector2 centerA = GetBodyWorldCenter(scene, m.A);
		const Vector2 centerB = GetBodyWorldCenter(scene, m.B);

		for (int ci = 0; ci < m.ContactCount; ++ci)
		{
			// 새 contact 는 누적 impulse 0 으로 시작.
			m.AccumulatedNormalImpulse[ci]   = 0.0f;
			m.AccumulatedFrictionImpulse[ci] = 0.0f;

			if (m.FeatureIds[ci] == 0xFFFFFFFFu) continue;

			auto it = prevTable.find(PrevKey{ key, m.FeatureIds[ci] });
			if (it == prevTable.end()) continue;

			// 매칭됨 — 누적 impulse 복원 + body 에 warm-start impulse 적용 (한 번만).
			m.AccumulatedNormalImpulse[ci]   = it->second.Norm;
			m.AccumulatedFrictionImpulse[ci] = it->second.Fric;

			const Vector2 warm = normal * it->second.Norm + tangent * it->second.Fric;
			ApplyImpulse(bodyA, centerA, -warm, m.ContactPoints[ci]);
			ApplyImpulse(bodyB, centerB,  warm, m.ContactPoints[ci]);
		}
	}
}

// ── 속도 해소 (Sequential Impulse, accumulated + clamped) ───────────────────
// Box2D / Erin Catto 의 정석 패턴:
//   1. 각 iteration 에서 delta impulse 계산
//   2. 누적 impulse 에 더하고 clamp (normal: [0,∞), friction: [-mu*accN, +mu*accN])
//   3. delta = newAccum - oldAccum 만큼만 body 에 적용
//
// 매 sub-step 마다 누적 impulse 가 초기화되던 이전 코드와 달리, 누적 impulse 는
// 매니폴드에 보존되어 MatchAndWarmStart 에서 다음 step 으로 전달된다.
// → friction clamp 가 매 step 0 부터 시작하지 않고 점차 커진 누적 normal 기준으로
//    동작하므로 friction 이 실제로 효과적으로 작용.

void CPhysics2DSystem::ResolveContactVelocity(CScene& scene)
{
	for (Physics2DManifold& manifold : m_manifolds)
	{
		if (manifold.IsTrigger || manifold.ContactCount <= 0) continue;

		Transform2D* transformA = scene.GetComponent<Transform2D>(manifold.A);
		Transform2D* transformB = scene.GetComponent<Transform2D>(manifold.B);
		if (!transformA || !transformB) continue;

		Rigidbody2D* bodyA = scene.GetComponent<Rigidbody2D>(manifold.A);
		Rigidbody2D* bodyB = scene.GetComponent<Rigidbody2D>(manifold.B);

		const Vector2 normal  = NormalizeSafe(manifold.Normal);
		const Vector2 tangent(-normal.y, normal.x);

		const float invMassA   = GetInverseMass(bodyA);
		const float invMassB   = GetInverseMass(bodyB);
		const float invMassSum = invMassA + invMassB;
		if (invMassSum <= 0.0f) continue;

		const float invInertiaA = GetInverseInertia(bodyA);
		const float invInertiaB = GetInverseInertia(bodyB);

		const Vector2 centerA = GetBodyWorldCenter(scene, manifold.A);
		const Vector2 centerB = GetBodyWorldCenter(scene, manifold.B);

		const float restitutionA = bodyA ? bodyA->Restitution : 0.0f;
		const float restitutionB = bodyB ? bodyB->Restitution : 0.0f;
		const float friction     = std::sqrt(std::max(0.0f,
		    GetSurfaceFriction(bodyA) * GetSurfaceFriction(bodyB)));

		for (int ci = 0; ci < manifold.ContactCount; ++ci)
		{
			const Vector2& contactPoint = manifold.ContactPoints[ci];
			const Vector2  ra = contactPoint - centerA;
			const Vector2  rb = contactPoint - centerB;

			// ── Normal impulse (incremental + clamp ≥ 0) ───────────────────
			const float raCrossN    = Cross(ra, normal);
			const float rbCrossN    = Cross(rb, normal);
			const float normalDenom = invMassSum
			    + raCrossN * raCrossN * invInertiaA
			    + rbCrossN * rbCrossN * invInertiaB;
			if (normalDenom > 0.0f)
			{
				const Vector2 vA        = GetContactVelocity(bodyA, centerA, contactPoint);
				const Vector2 vB        = GetContactVelocity(bodyB, centerB, contactPoint);
				const float          velAlongN = Dot(vB - vA, normal);

				// restitution: 저속 충돌은 0 (미세 진동 방지)
				const float restitution = (-velAlongN > RESTITUTION_VELOCITY_THRESHOLD)
				                          ? std::max(restitutionA, restitutionB) : 0.0f;

				// delta impulse 계산
				const float deltaMag = -(1.0f + restitution) * velAlongN / normalDenom;

				// 누적 impulse 에 더하고 [0, ∞) 로 clamp
				const float oldAcc = manifold.AccumulatedNormalImpulse[ci];
				const float newAcc = std::max(0.0f, oldAcc + deltaMag);
				const float applyMag = newAcc - oldAcc;
				manifold.AccumulatedNormalImpulse[ci] = newAcc;

				if (std::abs(applyMag) > 0.0f)
				{
					const Vector2 impulse = normal * applyMag;
					AddImpulseDebug(bodyA, centerA, -impulse, normal, contactPoint, false);
					AddImpulseDebug(bodyB, centerB,  impulse, normal, contactPoint, false);
					ApplyImpulse(bodyA, centerA, -impulse, contactPoint);
					ApplyImpulse(bodyB, centerB,  impulse, contactPoint);
				}
			}

			// ── Friction impulse (incremental + clamp |.| ≤ mu * accumN) ───
			const float raCrossT    = Cross(ra, tangent);
			const float rbCrossT    = Cross(rb, tangent);
			const float tangentDenom = invMassSum
			    + raCrossT * raCrossT * invInertiaA
			    + rbCrossT * rbCrossT * invInertiaB;
			if (tangentDenom > 0.0f)
			{
				const Vector2 vA = GetContactVelocity(bodyA, centerA, contactPoint);
				const Vector2 vB = GetContactVelocity(bodyB, centerB, contactPoint);
				const float velAlongT = Dot(vB - vA, tangent);

				const float deltaMag = -velAlongT / tangentDenom;

				// 누적 friction 을 mu * accumN 으로 clamp.
				// floor 적용 — resting contact 에서 normalImp 가 0 에 가까울 때도 minimum 보장.
				const float accNForFriction = std::max(
				    manifold.AccumulatedNormalImpulse[ci], FRICTION_NORMAL_IMPULSE_FLOOR);
				const float frictionMax = friction * accNForFriction;
				const float oldAcc = manifold.AccumulatedFrictionImpulse[ci];
				const float newAcc = std::clamp(oldAcc + deltaMag, -frictionMax, +frictionMax);
				const float applyMag = newAcc - oldAcc;
				manifold.AccumulatedFrictionImpulse[ci] = newAcc;

				if (std::abs(applyMag) > 0.0f)
				{
					const Vector2 frictionImpulse = tangent * applyMag;
					AddImpulseDebug(bodyA, centerA, -frictionImpulse, normal, contactPoint, true);
					AddImpulseDebug(bodyB, centerB,  frictionImpulse, normal, contactPoint, true);
					ApplyImpulse(bodyA, centerA, -frictionImpulse, contactPoint);
					ApplyImpulse(bodyB, centerB,  frictionImpulse, contactPoint);
				}
			}
		}
	}
}

// ── 위치 보정 ─────────────────────────────────────────────────────────────────
// 반드시 매니폴드 단위(쌍 단위)로 1회만 적용한다.
// 접촉점 수에 무관하게 침투 보정량은 항상 동일 — 다중 접촉점에서 과보정 없음.
//
// 같은 (A,B) 페어의 중복 매니폴드는 DetectContacts 끝에서 이미 1개로 병합되므로
// 여기서는 모든 매니폴드에 standard 보정만 적용한다.

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

		const Vector2 normal     = NormalizeSafe(manifold.Normal);
		const float          corrDepth  = std::max(manifold.Penetration - POSITION_CORRECTION_SLOP, 0.0f);
		const Vector2 correction = normal * (corrDepth / invMassSum * POSITION_CORRECTION_PERCENT);

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
			body.Velocity = Vector2(0.0f, 0.0f);
			body.Force    = Vector2(0.0f, 0.0f);
		}

		// Tangential (미끄러짐) 속도가 작으면 강제 0.
		// contact normal 방향(예: 위) 의 속도는 유지하되 그 수직 방향(미끄러짐) 만 제거.
		// 작은 friction 누적의 일관된 한쪽 편향이 등속 운동을 만드는 사용자 케이스 차단.
		{
			const Vector2 n = NormalizeSafe(body.LastContactNormal, Vector2(0.0f, 0.0f));
			if (n.LengthSqrt() > MIN_NORMAL_LENGTH_SQ)
			{
				const float          velAlongN = Dot(body.Velocity, n);
				const Vector2 tangent   = body.Velocity - n * velAlongN;
				if (tangent.LengthSqrt() <= RESTING_TANGENT_VELOCITY * RESTING_TANGENT_VELOCITY)
				{
					body.Velocity = n * velAlongN;
				}
			}
		}
	});
}
