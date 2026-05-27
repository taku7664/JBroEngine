#pragma once

// Shared utility so every system that needs world-space transforms calls the
// same function instead of duplicating CalculateWorldTransform locally.
//
// Fast path: if CTransformSystem has already run this frame, each entity has a
// WorldTransform2D component containing the pre-computed matrix — O(1) lookup.
//
// Fallback: walk the parent chain directly.  Used when TransformSystem has not
// been added to the scene (e.g. in unit tests or one-off tool scenes).

#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Component/WorldTransform2D.h"
#include "GameFramework/ECS/EntityTypes.h"
#include "GameFramework/Scene/Scene.h"
#include "Utillity/Matrix3x2.h"

inline Matrix3x2 GetWorldTransform(const CScene& scene, EntityId entity)
{
	// ── Fast path: cached by CTransformSystem ────────────────────────────────
	const WorldTransform2D* cached = scene.GetComponent<WorldTransform2D>(entity);
	if (cached)
	{
		return cached->Matrix;
	}

	// ── Fallback: traverse parent chain ──────────────────────────────────────
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
