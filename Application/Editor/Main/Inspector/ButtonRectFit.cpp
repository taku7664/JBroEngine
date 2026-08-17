#include "pch.h"
#include "ButtonRectFit.h"

#include "Engine/GameFramework/Canvas/CanvasTransformUtils.h"
#include "Engine/GameFramework/Component/Button2D.h"
#include "Engine/GameFramework/Component/Renderer2DComponent.h"
#include "Engine/GameFramework/Object/GameObject.h"

#include <algorithm>
#include <limits>

namespace
{
	struct Accumulator
	{
		float MinX =  std::numeric_limits<float>::max();
		float MinY =  std::numeric_limits<float>::max();
		float MaxX = -std::numeric_limits<float>::max();
		float MaxY = -std::numeric_limits<float>::max();
		bool  Any  = false;

		void Add(const Vector2& point)
		{
			MinX = std::min(MinX, point.x);
			MinY = std::min(MinY, point.y);
			MaxX = std::max(MaxX, point.x);
			MaxY = std::max(MaxY, point.y);
			Any = true;
		}
	};

	// object 의 렌더러 경계를 buttonLocal 공간으로 옮겨 담는다.
	// 자식은 트랜스폼이 다르므로 오브젝트 로컬 → 월드 → 버튼 로컬 로 두 번 지난다.
	void AddRenderers(const CGameObject& object, const Matrix3x2& worldToButtonLocal, Accumulator& out)
	{
		const Matrix3x2 objectToWorld = GetWorldTransform(object);

		for (const SafePtr<CComponent>& component : object.GetComponents())
		{
			if (false == component.IsValid() || false == component->IsEnabled())
			{
				continue;
			}
			const CRenderer2DComponent* renderer = component->AsRenderer2DComponent();
			if (nullptr == renderer)
			{
				continue;
			}

			const Rect& local = renderer->GetLocalBounds();
			if (local.Left >= local.Right || local.Top >= local.Bottom)
			{
				continue;   // 아직 자산이 안 풀렸거나 빈 경계 — 합집합을 오염시키지 않는다.
			}

			// 회전한 렌더러는 축정렬 상자가 아니다 — 네 꼭짓점을 다 옮겨야 한다.
			const Vector2 corners[4] = {
				Vector2(local.Left,  local.Top),
				Vector2(local.Right, local.Top),
				Vector2(local.Right, local.Bottom),
				Vector2(local.Left,  local.Bottom),
			};
			for (const Vector2& corner : corners)
			{
				out.Add(worldToButtonLocal.TransformPoint(objectToWorld.TransformPoint(corner)));
			}
		}
	}

	void Collect(const CGameObject& object, const Matrix3x2& worldToButtonLocal, bool isRoot, Accumulator& out)
	{
		if (false == object.IsActiveInHierarchy())
		{
			return;   // 보이는 것이 눌리는 것이다.
		}
		// 자기 버튼을 가진 자식은 통째로 건너뛴다 — 그 영역은 그 버튼 소관이다.
		if (false == isRoot && nullptr != const_cast<CGameObject&>(object).GetComponent<Button2D>())
		{
			return;
		}

		AddRenderers(object, worldToButtonLocal, out);

		for (const SafePtr<CGameObject>& child : object.GetChildren())
		{
			if (child.IsValid())
			{
				Collect(*child, worldToButtonLocal, false, out);
			}
		}
	}
}

bool ButtonRectFit::ComputeRendererUnion(const CGameObject& buttonObject, Rect& outLocalBounds)
{
	Matrix3x2 worldToButtonLocal;
	if (false == GetWorldTransform(buttonObject).TryInvert(worldToButtonLocal))
	{
		return false;   // 스케일 0 — 옮겨 담을 공간이 없다.
	}

	Accumulator accumulator;
	Collect(buttonObject, worldToButtonLocal, true, accumulator);
	if (false == accumulator.Any)
	{
		return false;
	}

	outLocalBounds = Rect(accumulator.MinX, accumulator.MinY, accumulator.MaxX, accumulator.MaxY);
	return true;
}
