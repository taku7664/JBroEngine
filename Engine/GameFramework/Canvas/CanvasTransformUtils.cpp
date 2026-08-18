#include "pch.h"
#include "GameFramework/Canvas/CanvasTransformUtils.h"

#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Object/GameObject.h"

Matrix3x2 GetWorldTransform(const CGameObject& object)
{
	return object.GetWorld().Matrix;
}

Matrix3x2 ComputeWorldTransformNow(const CGameObject& object)
{
	Matrix3x2 world = object.GetTransform().ToMatrix3x2();

	// 루트까지 거슬러 올라가며 부모 로컬을 곱한다. 루트가 누구인지 알아야 아래에서 앵커를
	// 물어볼 수 있으므로 노드를 따라가며 기억한다.
	const CGameObject* root = &object;
	while (const CGameObject* parent = root->GetParent().TryGet())
	{
		world = world * parent->GetTransform().ToMatrix3x2();
		root = parent;
	}

	// CTransformSystem 이 루트에 넣는 부모 행렬과 같은 것을 태운다(화면 공간 레이어의 앵커
	// 평행이동). World 레이어에서는 (0,0) 이라 항등이고, 화면 공간에서만 실제로 이동한다.
	if (const CGameCanvas* canvas = root->GetCanvas())
	{
		const Vector2 anchorOffset = canvas->GetRootAnchorOffset(*root);
		if (0.0f != anchorOffset.x || 0.0f != anchorOffset.y)
		{
			world = world * Matrix3x2::Transform(anchorOffset, Radian(0.0f), Vector2(1.0f, 1.0f));
		}
	}
	return world;
}
