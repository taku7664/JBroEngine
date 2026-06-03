#include "pch.h"
#include "TransformSystem.h"

#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Scene/Scene.h"
#include "Utillity/Math/Matrix3x2.h"

namespace
{
	// 부모 → 자식으로 월드 트랜스폼을 전파한다. parentWorld 는 부모의 최종 월드 행렬.
	void PropagateWorldTransform(CGameObject& object, const Matrix3x2& parentWorld)
	{
		const Matrix3x2 worldMatrix = object.GetTransform().ToMatrix3x2() * parentWorld;
		object.GetWorld().Matrix = worldMatrix;

		for (const SafePtr<CGameObject>& childRef : object.GetChildren())
		{
			if (CGameObject* child = childRef.TryGet())
			{
				PropagateWorldTransform(*child, worldMatrix);
			}
		}
	}
} // anonymous namespace

void CTransformSystem::OnUpdate(CScene& scene)
{
	// 루트(부모 없음)만 처리 — 자손은 PropagateWorldTransform 내부에서 재귀 처리.
	scene.ForEachObject([](CGameObject& object)
	{
		if (false == object.GetParent().IsValid())
		{
			PropagateWorldTransform(object, Matrix3x2::Identity());
		}
	});
}
