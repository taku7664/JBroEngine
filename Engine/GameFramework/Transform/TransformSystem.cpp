#include "pch.h"
#include "TransformSystem.h"

#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Component/TransformHierarchy2D.h"
#include "GameFramework/Component/WorldTransform2D.h"
#include "GameFramework/Scene/Scene.h"
#include "Utillity/Matrix3x2.h"

namespace
{
	// Recursively propagates world transforms from parent to children.
	// parentWorld is already the final world matrix of the parent entity.
	void PropagateWorldTransform(CScene& scene, EntityId entity, const Matrix3x2& parentWorld)
	{
		const Transform2D* local = scene.GetComponent<Transform2D>(entity);
		const Matrix3x2 worldMatrix = local
			? (local->ToMatrix3x2() * parentWorld)
			: parentWorld;

		WorldTransform2D* wt = scene.GetComponent<WorldTransform2D>(entity);
		if (!wt)
		{
			wt = scene.AddComponent<WorldTransform2D>(entity);
		}
		if (wt)
		{
			wt->Matrix = worldMatrix;
		}

		// Propagate to children in sibling-list order.
		EntityId child = scene.GetFirstChild(entity);
		while (INVALID_ENTITY_ID != child)
		{
			PropagateWorldTransform(scene, child, worldMatrix);
			child = scene.GetNextSibling(child);
		}
	}
} // anonymous namespace

void CTransformSystem::OnUpdate(CScene& scene)
{
	// Only root entities (no parent) are processed here;
	// their descendants are handled recursively inside PropagateWorldTransform.
	scene.ForEach<Transform2D>(
		[&scene](EntityId entity, const Transform2D& transform)
		{
			if (INVALID_ENTITY_ID == scene.GetParent(entity))
			{
				PropagateWorldTransform(scene, entity, Matrix3x2::Identity());
			}
		});
}
