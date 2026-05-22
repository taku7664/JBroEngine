#pragma once

#include "GameFramework/Physics2D/Physics2DTypes.h"
#include "GameFramework/System/GameSystem.h"

class CPhysics2DSystem final : public CGameSystem
{
public:
	void SetGravity(const Vector2<float>& gravity);
	const Vector2<float>& GetGravity() const;
	void SetFixedTimeStep(float fixedTimeStep);
	float GetFixedTimeStep() const;
	const std::vector<Physics2DContact>& GetContacts() const;

protected:
	void OnUpdate(CScene& scene) override;

private:
	void Step(CScene& scene, float deltaSeconds);
	void IntegrateBodies(CScene& scene, float deltaSeconds);
	void UpdateColliderBounds(CScene& scene);
	void DetectContacts(CScene& scene);
	void ResolveContacts(CScene& scene);

private:
	Vector2<float> m_gravity = Vector2<float>(0.0f, -9.8f);
	float m_fixedTimeStep = 1.0f / 60.0f;
	float m_accumulator = 0.0f;
	std::vector<Physics2DContact> m_contacts;
};
