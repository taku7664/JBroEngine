#include "pch.h"
#include "Physics2DSystem.h"

#include "Core/Core.h"
#include "Core/Time/Time.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Physics2D/Physics2DTypes.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneTransformUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	// ── Solver 상수 ───────────────────────────────────────────────────────────────
	//
	// POSITION_CORRECTION_PERCENT:
	//   한 스텝에서 침투량의 몇 %를 보정할지.  너무 높으면 과보정(떨림),
	//   너무 낮으면 물체가 서서히 가라앉음.  0.2 ≈ 5프레임에 걸쳐 완전히 보정.
	//
	// POSITION_CORRECTION_SLOP:
	//   이 이하의 침투는 무시.  미세한 수치 오차로 인한 보정 진동 방지.
	//
	// RESTITUTION_VELOCITY_THRESHOLD:
	//   충돌 속도가 이 이하면 반발계수를 0으로 취급.
	//   정지 물체가 미세하게 튀는 현상 방지.
	//
	constexpr float MIN_PHYSICS_DELTA_SECONDS      = 0.0001f;
	constexpr float POSITION_CORRECTION_PERCENT    = 0.2f;
	constexpr float POSITION_CORRECTION_SLOP       = 0.01f;
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

	bool TestPolygonSAT(const std::vector<Vector2<float>>& a, const std::vector<Vector2<float>>& b,
	                    Vector2<float>& outNormal, float& outPenetration)
	{
		outPenetration = std::numeric_limits<float>::max();
		for (int shape = 0; shape < 2; ++shape)
		{
			const std::vector<Vector2<float>>& points = (shape == 0) ? a : b;
			for (std::size_t i = 0; i < points.size(); ++i)
			{
				const Vector2<float>& p0      = points[i];
				const Vector2<float>& p1      = points[(i + 1) % points.size()];
				const Vector2<float>  edge    = p1 - p0;
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
					return false;
				}
				if (overlap < outPenetration)
				{
					outPenetration = overlap;
					outNormal      = axis;
				}
			}
		}
		return true;
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

	void ApplyVelocityToTransform(Transform2D& transform, Rigidbody2D& body, float dt)
	{
		Vector2<float> delta = body.Velocity * dt;
		if (body.FreezePositionX) { delta.x = 0.0f; body.Velocity.x = 0.0f; }
		if (body.FreezePositionY) { delta.y = 0.0f; body.Velocity.y = 0.0f; }
		transform.Position += delta;

		if (body.FreezeRotation)
		{
			body.AngularVelocity = 0.0f;
			body.Torque          = 0.0f;
			return;
		}
		transform.RotationRadians += body.AngularVelocity * dt;
	}

	void ApplyCorrectionToTransform(Transform2D& transform, Rigidbody2D* body,
	                                const Vector2<float>& correction)
	{
		Vector2<float> delta = correction;
		if (body)
		{
			if (body->FreezePositionX) delta.x = 0.0f;
			if (body->FreezePositionY) delta.y = 0.0f;
		}
		transform.Position += delta;
	}

	Vector2<float> GetBodyWorldCenter(const CScene& scene, EntityId entity)
	{
		const Matrix3x2 worldTransform = GetWorldTransform(scene, entity);
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

} // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────────────

void CPhysics2DSystem::SetGravity(const Vector2<float>& gravity)             { m_gravity = gravity; }
const Vector2<float>& CPhysics2DSystem::GetGravity() const                  { return m_gravity; }

void CPhysics2DSystem::SetVelocityIterations(int it)  { m_velocityIterations = std::max(1, it); }
int  CPhysics2DSystem::GetVelocityIterations() const  { return m_velocityIterations; }

void CPhysics2DSystem::SetPositionIterations(int it)  { m_positionIterations = std::max(1, it); }
int  CPhysics2DSystem::GetPositionIterations() const  { return m_positionIterations; }

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
	Step(scene, fixedDelta);
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
	scene.ForEach<Transform2D, Rigidbody2D>([this, deltaSeconds](EntityId, Transform2D& transform, Rigidbody2D& body)
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
			ApplyVelocityToTransform(transform, body, deltaSeconds);
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

		ApplyVelocityToTransform(transform, body, deltaSeconds);
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
			return;
		}
		const Matrix3x2 wt = GetWorldTransform(scene, entity);
		collider.BuildLocalPoints(collider.LocalPoints);
		collider.WorldPoints.clear();
		collider.WorldPoints.reserve(collider.LocalPoints.size());
		for (const Vector2<float>& p : collider.LocalPoints)
		{
			collider.WorldPoints.push_back(wt.TransformPoint(p));
		}
		collider.WorldAABB = CalculateAABB(collider.WorldPoints);
	});

	// Pass 1b: 원 월드 기하
	scene.ForEach<CircleCollider2D>([&scene](EntityId entity, CircleCollider2D& collider)
	{
		if (!collider.IsEnabled) return;
		const Matrix3x2 wt    = GetWorldTransform(scene, entity);
		collider.WorldCenter  = wt.TransformPoint(collider.LocalCenter);
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
	for (std::size_t i = 0; i < polygons.size(); ++i)
	{
		for (std::size_t j = i + 1; j < polygons.size(); ++j)
		{
			const EntityId ea = polygons[i].first;
			const EntityId eb = polygons[j].first;
			if (ea == eb) continue; // 자기 충돌 방지

			PolygonCollider2D* a = polygons[i].second;
			PolygonCollider2D* b = polygons[j].second;
			if (!IntersectsAABB(a->WorldAABB, b->WorldAABB)) continue;

			Vector2<float> normal;
			float          penetration;
			if (!TestPolygonSAT(a->WorldPoints, b->WorldPoints, normal, penetration)) continue;

			// 실제 콜라이더 중심으로 법선 방향 결정 (8순위 수정)
			const Vector2<float> centerA = CalculateCenter(a->WorldPoints);
			const Vector2<float> centerB = CalculateCenter(b->WorldPoints);
			OrientNormal(normal, centerA, centerB);

			Physics2DManifold manifold;
			manifold.A           = ea;
			manifold.B           = eb;
			manifold.Normal      = normal;
			manifold.Penetration = penetration;
			manifold.IsTrigger   = a->IsTrigger || b->IsTrigger;

			const ContactManifold2D cm = BuildPolygonManifold(a->WorldPoints, b->WorldPoints, normal);
			manifold.ContactCount = cm.Count;
			for (int mi = 0; mi < cm.Count; ++mi)
			{
				manifold.ContactPoints[mi] = cm.Points[mi];
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
	for (auto& [pe, polygon] : polygons)
	{
		for (auto& [ce, circle] : circles)
		{
			if (pe == ce) continue;
			if (!IntersectsAABB(polygon->WorldAABB, circle->WorldAABB)) continue;

			Vector2<float> normal;
			float          penetration;
			if (!TestPolygonCircle(*polygon, *circle, normal, penetration)) continue;

			// 실제 폴리곤/원 중심으로 법선 방향 결정
			const Vector2<float> centerA = CalculateCenter(polygon->WorldPoints);
			OrientNormal(normal, centerA, circle->WorldCenter);

			Physics2DManifold manifold;
			manifold.A           = pe;
			manifold.B           = ce;
			manifold.Normal      = normal;
			manifold.Penetration = penetration;
			manifold.IsTrigger   = polygon->IsTrigger || circle->IsTrigger;
			// 법선 방향 확정 이후 접촉점 계산 (6순위 수정)
			manifold.ContactPoints[0] = circle->WorldCenter - normal * circle->WorldRadius;
			manifold.ContactCount     = 1;

			m_manifolds.push_back(manifold);
		}
	}
}

// ── 속도 해소 ─────────────────────────────────────────────────────────────────
// 매니폴드 단위로 순회하되, impulse는 접촉점별로 독립 계산한다.
// 두 접촉점이 서로 다른 모멘트 암(ra, rb)을 가지므로
// 각각 계산하는 것이 회전 정확도에 유리하다.

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
			if (velAlongN >= 0.0f) continue; // 분리 중 — 건너뜀

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

		ApplyCorrectionToTransform(*transformA, bodyA, -correction * invMassA);
		ApplyCorrectionToTransform(*transformB, bodyB,  correction * invMassB);
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
