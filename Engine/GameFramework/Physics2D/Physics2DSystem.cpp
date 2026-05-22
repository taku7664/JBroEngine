#include "pch.h"
#include "Physics2DSystem.h"

#include "Core/Core.h"
#include "Core/Time/Time.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Physics2D/Physics2DTypes.h"
#include "GameFramework/Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	constexpr float MIN_PHYSICS_DELTA_SECONDS = 0.0001f;
	constexpr float MAX_PHYSICS_FRAME_SECONDS = 0.25f;
	constexpr int MAX_PHYSICS_SUB_STEPS = 8;
	constexpr float POSITION_CORRECTION_PERCENT = 0.8f;
	constexpr float POSITION_CORRECTION_SLOP = 0.01f;
	constexpr float MIN_NORMAL_LENGTH_SQ = 0.000001f;

	float Dot(const Vector2<float>& a, const Vector2<float>& b)
	{
		return a.x * b.x + a.y * b.y;
	}

	bool IntersectsAABB(const PhysicsAABB2D& a, const PhysicsAABB2D& b)
	{
		return a.Min.x <= b.Max.x && a.Max.x >= b.Min.x && a.Min.y <= b.Max.y && a.Max.y >= b.Min.y;
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

	void ProjectPolygon(const std::vector<Vector2<float>>& points, const Vector2<float>& axis, float& outMin, float& outMax)
	{
		outMin = Dot(points[0], axis);
		outMax = outMin;
		for (const Vector2<float>& point : points)
		{
			const float projection = Dot(point, axis);
			outMin = std::min(outMin, projection);
			outMax = std::max(outMax, projection);
		}
	}

	bool TestPolygonSAT(const std::vector<Vector2<float>>& a, const std::vector<Vector2<float>>& b, Vector2<float>& outNormal, float& outPenetration)
	{
		outPenetration = std::numeric_limits<float>::max();
		for (int shape = 0; shape < 2; ++shape)
		{
			const std::vector<Vector2<float>>& points = shape == 0 ? a : b;
			for (std::size_t i = 0; i < points.size(); ++i)
			{
				const Vector2<float>& p0 = points[i];
				const Vector2<float>& p1 = points[(i + 1) % points.size()];
				Vector2<float> edge = p1 - p0;
				Vector2<float> axis(-edge.y, edge.x);
				axis.Normalize();

				float minA = 0.0f;
				float maxA = 0.0f;
				float minB = 0.0f;
				float maxB = 0.0f;
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
					outNormal = axis;
				}
			}
		}
		return true;
	}

	bool TestCircleCircle(const CircleCollider2D& a, const CircleCollider2D& b, Vector2<float>& outNormal, float& outPenetration)
	{
		const Vector2<float> delta = b.WorldCenter - a.WorldCenter;
		const float distanceSq = delta.LengthSqrt();
		const float radiusSum = a.WorldRadius + b.WorldRadius;
		if (distanceSq > radiusSum * radiusSum)
		{
			return false;
		}

		const float distance = std::sqrt(distanceSq);
		outNormal = distance > 0.0f ? delta / distance : Vector2<float>(1.0f, 0.0f);
		outPenetration = radiusSum - distance;
		return true;
	}

	bool TestPolygonCircle(const PolygonCollider2D& polygon, const CircleCollider2D& circle, Vector2<float>& outNormal, float& outPenetration)
	{
		if (polygon.WorldPoints.size() < 3)
		{
			return false;
		}

		outPenetration = std::numeric_limits<float>::max();
		for (std::size_t i = 0; i < polygon.WorldPoints.size(); ++i)
		{
			const Vector2<float>& p0 = polygon.WorldPoints[i];
			const Vector2<float>& p1 = polygon.WorldPoints[(i + 1) % polygon.WorldPoints.size()];
			Vector2<float> edge = p1 - p0;
			Vector2<float> axis(-edge.y, edge.x);
			axis.Normalize();

			float minPolygon = 0.0f;
			float maxPolygon = 0.0f;
			ProjectPolygon(polygon.WorldPoints, axis, minPolygon, maxPolygon);
			const float circleProjection = Dot(circle.WorldCenter, axis);
			const float minCircle = circleProjection - circle.WorldRadius;
			const float maxCircle = circleProjection + circle.WorldRadius;
			const float overlap = std::min(maxPolygon, maxCircle) - std::max(minPolygon, minCircle);
			if (overlap <= 0.0f)
			{
				return false;
			}
			if (overlap < outPenetration)
			{
				outPenetration = overlap;
				outNormal = axis;
			}
		}
		return true;
	}

	Matrix3x2 CalculateWorldTransform(const CScene& scene, EntityId entity)
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

	float GetInverseMass(const Rigidbody2D* body)
	{
		if (nullptr == body || EPhysics2DBodyType::Dynamic != body->BodyType || body->Mass <= 0.0f)
		{
			return 0.0f;
		}

		return 1.0f / body->Mass;
	}

	void ApplyVelocityToTransform(Transform2D& transform, Rigidbody2D& body, float deltaSeconds)
	{
		Vector2<float> delta = body.Velocity * deltaSeconds;
		if (body.FreezePositionX)
		{
			delta.x = 0.0f;
			body.Velocity.x = 0.0f;
		}
		if (body.FreezePositionY)
		{
			delta.y = 0.0f;
			body.Velocity.y = 0.0f;
		}

		transform.Position += delta;
	}

	void ApplyCorrectionToTransform(Transform2D& transform, Rigidbody2D* body, const Vector2<float>& correction)
	{
		Vector2<float> delta = correction;
		if (body)
		{
			if (body->FreezePositionX)
			{
				delta.x = 0.0f;
			}
			if (body->FreezePositionY)
			{
				delta.y = 0.0f;
			}
		}

		transform.Position += delta;
	}

	void OrientContactNormal(CScene& scene, Physics2DContact& contact)
	{
		const Transform2D* transformA = scene.GetComponent<Transform2D>(contact.A);
		const Transform2D* transformB = scene.GetComponent<Transform2D>(contact.B);
		if (nullptr == transformA || nullptr == transformB)
		{
			return;
		}

		const Vector2<float> delta = transformB->Position - transformA->Position;
		if (Dot(delta, contact.Normal) < 0.0f)
		{
			contact.Normal = -contact.Normal;
		}
	}
}

void CPhysics2DSystem::SetGravity(const Vector2<float>& gravity)
{
	m_gravity = gravity;
}

const Vector2<float>& CPhysics2DSystem::GetGravity() const
{
	return m_gravity;
}

void CPhysics2DSystem::SetFixedTimeStep(float fixedTimeStep)
{
	if (fixedTimeStep >= MIN_PHYSICS_DELTA_SECONDS)
	{
		m_fixedTimeStep = fixedTimeStep;
	}
}

float CPhysics2DSystem::GetFixedTimeStep() const
{
	return m_fixedTimeStep;
}

const std::vector<Physics2DContact>& CPhysics2DSystem::GetContacts() const
{
	return m_contacts;
}

void CPhysics2DSystem::OnUpdate(CScene& scene)
{
	const float deltaSeconds = Core::Time ? Core::Time->GetDeltaSeconds() : 0.0f;
	if (deltaSeconds <= 0.0f)
	{
		UpdateColliderBounds(scene);
		DetectContacts(scene);
		return;
	}

	m_accumulator += std::min(deltaSeconds, MAX_PHYSICS_FRAME_SECONDS);

	int stepCount = 0;
	while (m_accumulator >= m_fixedTimeStep && stepCount < MAX_PHYSICS_SUB_STEPS)
	{
		Step(scene, m_fixedTimeStep);
		m_accumulator -= m_fixedTimeStep;
		++stepCount;
	}

	if (stepCount >= MAX_PHYSICS_SUB_STEPS)
	{
		m_accumulator = 0.0f;
	}
}

void CPhysics2DSystem::Step(CScene& scene, float deltaSeconds)
{
	IntegrateBodies(scene, deltaSeconds);
	UpdateColliderBounds(scene);
	DetectContacts(scene);
	ResolveContacts(scene);
	UpdateColliderBounds(scene);
	DetectContacts(scene);
}

void CPhysics2DSystem::IntegrateBodies(CScene& scene, float deltaSeconds)
{
	scene.ForEach<Transform2D, Rigidbody2D>([this, deltaSeconds](EntityId, Transform2D& transform, Rigidbody2D& body) {
		if (EPhysics2DBodyType::Static == body.BodyType)
		{
			return;
		}

		if (EPhysics2DBodyType::Kinematic == body.BodyType)
		{
			ApplyVelocityToTransform(transform, body, deltaSeconds);
			return;
		}

		const float inverseMass = GetInverseMass(&body);
		if (inverseMass <= 0.0f)
		{
			return;
		}

		if (body.UseGravity)
		{
			body.Velocity += m_gravity * body.GravityScale * deltaSeconds;
		}
		body.Velocity += body.Force * inverseMass * deltaSeconds;
		if (body.LinearDamping > 0.0f)
		{
			body.Velocity *= std::max(0.0f, 1.0f - body.LinearDamping * deltaSeconds);
		}

		ApplyVelocityToTransform(transform, body, deltaSeconds);
		body.Force = Vector2<float>(0.0f, 0.0f);
		});
}

void CPhysics2DSystem::UpdateColliderBounds(CScene& scene)
{
	scene.ForEach<Transform2D, PolygonCollider2D>([&scene](EntityId entity, const Transform2D&, PolygonCollider2D& collider) {
		const Matrix3x2 worldTransform = CalculateWorldTransform(scene, entity);
		collider.WorldPoints.clear();
		collider.WorldPoints.reserve(collider.LocalPoints.size());
		for (const Vector2<float>& point : collider.LocalPoints)
		{
			collider.WorldPoints.push_back(worldTransform.TransformPoint(point));
		}
		collider.WorldAABB = CalculateAABB(collider.WorldPoints);
		});

	scene.ForEach<Transform2D, CircleCollider2D>([&scene](EntityId entity, const Transform2D&, CircleCollider2D& collider) {
		const Matrix3x2 worldTransform = CalculateWorldTransform(scene, entity);
		collider.WorldCenter = worldTransform.TransformPoint(collider.LocalCenter);
		const float scaleX = std::sqrt(worldTransform.M11 * worldTransform.M11 + worldTransform.M12 * worldTransform.M12);
		const float scaleY = std::sqrt(worldTransform.M21 * worldTransform.M21 + worldTransform.M22 * worldTransform.M22);
		const float scale = std::max(scaleX, scaleY);
		collider.WorldRadius = collider.Radius * scale;
		collider.WorldAABB.Min = Vector2<float>(collider.WorldCenter.x - collider.WorldRadius, collider.WorldCenter.y - collider.WorldRadius);
		collider.WorldAABB.Max = Vector2<float>(collider.WorldCenter.x + collider.WorldRadius, collider.WorldCenter.y + collider.WorldRadius);
		});
}

void CPhysics2DSystem::DetectContacts(CScene& scene)
{
	m_contacts.clear();
	std::vector<EntityId> polygonEntities;
	scene.ForEach<PolygonCollider2D>([&polygonEntities](EntityId entity, const PolygonCollider2D&) {
		polygonEntities.push_back(entity);
		});
	std::vector<EntityId> circleEntities;
	scene.ForEach<CircleCollider2D>([&circleEntities](EntityId entity, const CircleCollider2D&) {
		circleEntities.push_back(entity);
		});

	for (std::size_t i = 0; i < polygonEntities.size(); ++i)
	{
		for (std::size_t j = i + 1; j < polygonEntities.size(); ++j)
		{
			PolygonCollider2D* a = scene.GetComponent<PolygonCollider2D>(polygonEntities[i]);
			PolygonCollider2D* b = scene.GetComponent<PolygonCollider2D>(polygonEntities[j]);
			if (nullptr == a || nullptr == b || a->WorldPoints.size() < 3 || b->WorldPoints.size() < 3)
			{
				continue;
			}
			if (false == IntersectsAABB(a->WorldAABB, b->WorldAABB))
			{
				continue;
			}

			Physics2DContact contact;
			contact.A = polygonEntities[i];
			contact.B = polygonEntities[j];
			contact.IsTrigger = a->IsTrigger || b->IsTrigger;
			if (TestPolygonSAT(a->WorldPoints, b->WorldPoints, contact.Normal, contact.Penetration))
			{
				OrientContactNormal(scene, contact);
				m_contacts.push_back(contact);
			}
		}
	}

	for (std::size_t i = 0; i < circleEntities.size(); ++i)
	{
		for (std::size_t j = i + 1; j < circleEntities.size(); ++j)
		{
			CircleCollider2D* a = scene.GetComponent<CircleCollider2D>(circleEntities[i]);
			CircleCollider2D* b = scene.GetComponent<CircleCollider2D>(circleEntities[j]);
			if (nullptr == a || nullptr == b || false == IntersectsAABB(a->WorldAABB, b->WorldAABB))
			{
				continue;
			}

			Physics2DContact contact;
			contact.A = circleEntities[i];
			contact.B = circleEntities[j];
			contact.IsTrigger = a->IsTrigger || b->IsTrigger;
			if (TestCircleCircle(*a, *b, contact.Normal, contact.Penetration))
			{
				OrientContactNormal(scene, contact);
				m_contacts.push_back(contact);
			}
		}
	}

	for (EntityId polygonEntity : polygonEntities)
	{
		for (EntityId circleEntity : circleEntities)
		{
			PolygonCollider2D* polygon = scene.GetComponent<PolygonCollider2D>(polygonEntity);
			CircleCollider2D* circle = scene.GetComponent<CircleCollider2D>(circleEntity);
			if (nullptr == polygon || nullptr == circle || false == IntersectsAABB(polygon->WorldAABB, circle->WorldAABB))
			{
				continue;
			}

			Physics2DContact contact;
			contact.A = polygonEntity;
			contact.B = circleEntity;
			contact.IsTrigger = polygon->IsTrigger || circle->IsTrigger;
			if (TestPolygonCircle(*polygon, *circle, contact.Normal, contact.Penetration))
			{
				OrientContactNormal(scene, contact);
				m_contacts.push_back(contact);
			}
		}
	}
}

void CPhysics2DSystem::ResolveContacts(CScene& scene)
{
	for (const Physics2DContact& contact : m_contacts)
	{
		if (contact.IsTrigger)
		{
			continue;
		}

		Transform2D* transformA = scene.GetComponent<Transform2D>(contact.A);
		Transform2D* transformB = scene.GetComponent<Transform2D>(contact.B);
		Rigidbody2D* bodyA = scene.GetComponent<Rigidbody2D>(contact.A);
		Rigidbody2D* bodyB = scene.GetComponent<Rigidbody2D>(contact.B);
		if (nullptr == transformA || nullptr == transformB)
		{
			continue;
		}

		Vector2<float> normal = contact.Normal;
		if (normal.LengthSqrt() <= MIN_NORMAL_LENGTH_SQ)
		{
			continue;
		}
		normal.Normalize();

		const float inverseMassA = GetInverseMass(bodyA);
		const float inverseMassB = GetInverseMass(bodyB);
		const float inverseMassSum = inverseMassA + inverseMassB;
		if (inverseMassSum <= 0.0f)
		{
			continue;
		}

		const Vector2<float> velocityA = bodyA ? bodyA->Velocity : Vector2<float>(0.0f, 0.0f);
		const Vector2<float> velocityB = bodyB ? bodyB->Velocity : Vector2<float>(0.0f, 0.0f);
		const Vector2<float> relativeVelocity = velocityB - velocityA;
		const float velocityAlongNormal = Dot(relativeVelocity, normal);
		if (velocityAlongNormal <= 0.0f)
		{
			const float restitutionA = bodyA ? bodyA->Restitution : 0.0f;
			const float restitutionB = bodyB ? bodyB->Restitution : 0.0f;
			const float restitution = std::min(restitutionA, restitutionB);
			const float impulseMagnitude = -(1.0f + restitution) * velocityAlongNormal / inverseMassSum;
			const Vector2<float> impulse = normal * impulseMagnitude;

			if (bodyA)
			{
				bodyA->Velocity -= impulse * inverseMassA;
			}
			if (bodyB)
			{
				bodyB->Velocity += impulse * inverseMassB;
			}

			const Vector2<float> frictionRelativeVelocity = (bodyB ? bodyB->Velocity : Vector2<float>(0.0f, 0.0f)) - (bodyA ? bodyA->Velocity : Vector2<float>(0.0f, 0.0f));
			Vector2<float> tangent = frictionRelativeVelocity - normal * Dot(frictionRelativeVelocity, normal);
			if (tangent.LengthSqrt() > MIN_NORMAL_LENGTH_SQ)
			{
				tangent.Normalize();
				float frictionMagnitude = -Dot(frictionRelativeVelocity, tangent) / inverseMassSum;
				const float frictionA = bodyA ? bodyA->Friction : 0.0f;
				const float frictionB = bodyB ? bodyB->Friction : 0.0f;
				const float friction = std::sqrt(std::max(0.0f, frictionA * frictionB));
				const float maxFrictionMagnitude = impulseMagnitude * friction;
				frictionMagnitude = std::clamp(frictionMagnitude, -maxFrictionMagnitude, maxFrictionMagnitude);

				const Vector2<float> frictionImpulse = tangent * frictionMagnitude;
				if (bodyA)
				{
					bodyA->Velocity -= frictionImpulse * inverseMassA;
				}
				if (bodyB)
				{
					bodyB->Velocity += frictionImpulse * inverseMassB;
				}
			}
		}

		const float correctionDepth = std::max(contact.Penetration - POSITION_CORRECTION_SLOP, 0.0f);
		const Vector2<float> correction = normal * (correctionDepth / inverseMassSum * POSITION_CORRECTION_PERCENT);
		ApplyCorrectionToTransform(*transformA, bodyA, -correction * inverseMassA);
		ApplyCorrectionToTransform(*transformB, bodyB, correction * inverseMassB);
	}
}
