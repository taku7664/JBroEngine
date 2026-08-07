#include "pch.h"
#include "TransformSystem.h"

#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Canvas/Canvas.h"
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

void CTransformSystem::OnUpdate(CGameCanvas& canvas)
{
	// 루트(부모 없음)만 처리 — 자손은 PropagateWorldTransform 내부에서 재귀 처리.
	//
	// 화면 공간 레이어의 루트는 항등 대신 **앵커점 평행이동**을 부모 행렬로 받는다.
	// 여기에 넣는 이유: Transform2D.Position 을 덮어쓰지 않으면서도 서브트리 전체가 자동으로
	// 따라온다. Position 을 매 프레임 계산된 값으로 갈아끼우면 인스펙터 편집·기즈모 드래그·
	// 스크립트 대입이 다음 프레임에 조용히 사라진다.
	canvas.ForEachObject([&canvas](CGameObject& object)
	{
		if (object.GetParent().IsValid())
		{
			return;
		}
		const Vector2 anchorOffset = canvas.GetRootAnchorOffset(object);
		const Matrix3x2 parentTransform = (0.0f == anchorOffset.x && 0.0f == anchorOffset.y)
			? Matrix3x2::Identity()
			: Matrix3x2::Transform(anchorOffset, Radian(0.0f), Vector2(1.0f, 1.0f));
		PropagateWorldTransform(object, parentTransform);
	});
}
