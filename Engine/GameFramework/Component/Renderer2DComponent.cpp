#include "pch.h"
#include "Renderer2DComponent.h"

#include "GameFramework/Object/GameObject.h"
#include "Utillity/Math/Matrix3x2.h"

#include <algorithm>

namespace
{
	// 로컬 경계의 네 모서리. 좌하 → 우하 → 우상 → 좌상 (Top=최소 y 규약).
	void GetLocalCorners(const Rect& bounds, Vector2 outCorners[4])
	{
		outCorners[0] = Vector2(bounds.Left, bounds.Top);
		outCorners[1] = Vector2(bounds.Right, bounds.Top);
		outCorners[2] = Vector2(bounds.Right, bounds.Bottom);
		outCorners[3] = Vector2(bounds.Left, bounds.Bottom);
	}

	Matrix3x2 GetOwnerWorldMatrix(const SafePtr<CGameObject>& owner)
	{
		const CGameObject* object = owner.TryGet();
		if (nullptr == object)
		{
			return Matrix3x2::Identity();
		}

		return object->GetWorld().Matrix;
	}
}

const Rect& CRenderer2DComponent::GetLocalBounds() const
{
	if (m_boundsDirty)
	{
		// 파생 타입이 경계를 낼 수 없으면 손대지 않는다 → 빈 Rect 가 그대로 남는다.
		m_localBounds = Rect();
		ComputeLocalBounds(m_localBounds);
		m_boundsDirty = false;
	}

	return m_localBounds;
}

Rect CRenderer2DComponent::GetWorldBounds() const
{
	const Rect& local = GetLocalBounds();
	if (local.IsEmpty())
	{
		return Rect();
	}

	Vector2 corners[4];
	GetWorldCorners(corners);

	Vector2 minPoint = corners[0];
	Vector2 maxPoint = corners[0];
	for (int i = 1; i < 4; ++i)
	{
		minPoint.x = std::min(minPoint.x, corners[i].x);
		minPoint.y = std::min(minPoint.y, corners[i].y);
		maxPoint.x = std::max(maxPoint.x, corners[i].x);
		maxPoint.y = std::max(maxPoint.y, corners[i].y);
	}

	// Top=최소 y 규약 유지.
	return Rect(minPoint.x, minPoint.y, maxPoint.x, maxPoint.y);
}

void CRenderer2DComponent::GetWorldCorners(Vector2 outCorners[4]) const
{
	GetLocalCorners(GetLocalBounds(), outCorners);

	const Matrix3x2 worldMatrix = GetOwnerWorldMatrix(GetOwner());
	for (int i = 0; i < 4; ++i)
	{
		outCorners[i] = worldMatrix.TransformPoint(outCorners[i]);
	}
}
