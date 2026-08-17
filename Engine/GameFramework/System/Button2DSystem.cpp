#include "pch.h"
#include "Button2DSystem.h"

#include "Core/EngineCore.h"
#include "Core/Input/InputDevices.h"
#include "Core/Input/InputSystem.h"
#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Canvas/CanvasTransformUtils.h"
#include "GameFramework/Canvas/GameLayer.h"
#include "GameFramework/Component/Button2D.h"
#include "GameFramework/Component/Renderer2DComponent.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Scripting/GameScript.h"

#include <cmath>

namespace
{
	// 히트 후보 정렬 키. 그리는 순서와 **같은 축**이어야 한다 —
	// 화면에선 UI 가 위인데 클릭은 뒤의 월드 버튼이 먹는 상황을 막는다.
	struct HitCandidate
	{
		CGameObject*  Object        = nullptr;
		bool          ScreenSpace   = false;
		int           LayerIndex    = -1;
		std::int32_t  SortOrder     = 0;
		std::uint64_t CreationOrder = 0;
	};

	// 화면 레이어는 Overlay 패스에서 라이팅 뒤에 그려지므로 인덱스 0 화면 레이어도
	// 인덱스 99 월드 레이어보다 위다. 그래서 **스페이스가 먼저**다.
	bool IsAbove(const HitCandidate& lhs, const HitCandidate& rhs)
	{
		if (lhs.ScreenSpace != rhs.ScreenSpace)
		{
			return lhs.ScreenSpace;
		}
		if (lhs.LayerIndex != rhs.LayerIndex)
		{
			return lhs.LayerIndex > rhs.LayerIndex;
		}
		if (lhs.SortOrder != rhs.SortOrder)
		{
			return lhs.SortOrder > rhs.SortOrder;
		}
		// 같은 자리에 겹쳤으면 나중에 만들어진 쪽이 위다(렌더 제출 순서와 같다).
		return lhs.CreationOrder > rhs.CreationOrder;
	}

	// 버튼은 렌더러가 아니라 SortOrder 가 없다. 같은 오브젝트의 렌더러에서 빌려 온다 —
	// 판정 순서를 **보이는 순서**에 맞추기 위해서다. 렌더러가 없으면 0.
	// (GetComponent<CRenderer2DComponent> 는 못 쓴다 — 조회가 정확한 타입명 키다.)
	std::int32_t BorrowSortOrder(const CGameObject& object)
	{
		for (const SafePtr<CComponent>& component : object.GetComponents())
		{
			if (false == component.IsValid())
			{
				continue;
			}
			if (const CRenderer2DComponent* renderer = component->AsRenderer2DComponent())
			{
				return renderer->SortOrder;
			}
		}
		return 0;
	}

	bool HitTest(const CGameObject& object, const Button2D& button, const Vector2& pointInLayer)
	{
		Matrix3x2 inverseWorld;
		if (false == GetWorldTransform(object).TryInvert(inverseWorld))
		{
			return false;   // 스케일 0 — 판정할 렉트가 없다.
		}

		// 역변환 한 번이라 회전·스케일·부모 계층이 그대로 따라온다(회전 버튼은 OBB).
		const Vector2 local = inverseWorld.TransformPoint(pointInLayer);
		const float halfX = std::abs(button.Size.x) * 0.5f;
		const float halfY = std::abs(button.Size.y) * 0.5f;
		return std::abs(local.x - button.Offset.x) <= halfX
			&& std::abs(local.y - button.Offset.y) <= halfY;
	}

	// 훅을 받을 자격 — 애니메이션 디스패치(SpriteAnimationSystem.cpp:111)와 같은 판정이다.
	bool CanReceive(const CGameScript& script)
	{
		const CGameObject* owner = script.GetOwner().TryGet();
		return script.IsEnabled() && nullptr != owner && owner->IsActiveInHierarchy();
	}
}

// ForEachScriptOnObject 는 CGameCanvas 의 private 이고 friend 는 이 클래스다 —
// 그래서 디스패치를 익명 네임스페이스 헬퍼로 빼지 않고 멤버로 둔다.
#define JBRO_DISPATCH_BUTTON_HOOK(NAME)                                              \
	canvas.ForEachScriptOnObject(object, [](CGameScript& script, CGameCanvas::ScriptRuntimeState&) \
	{                                                                                \
		if (CanReceive(script)) { script.NAME(); }                                   \
	})

CButton2DSystem::Pointer CButton2DSystem::ReadPointer()
{
	Pointer pointer;
	if (false == Engine.InputSystem.IsValid())
	{
		return pointer;
	}

	const InputDeviceContext& context = Engine.InputSystem->GetDeviceContext();

	// 터치가 마우스보다 우선이다 — 터치 기기에서는 마우스 좌표가 마지막 탭 자리에 굳어 있어
	// 손을 뗀 뒤에도 그 자리를 계속 호버로 잡는다.
	const Touch& touch = context.GetTouch();
	for (int index = 0; index < touch.GetCount(); ++index)
	{
		const TouchPoint& point = touch.Get(index);
		if (point.Active)
		{
			pointer.X = static_cast<float>(point.X);
			pointer.Y = static_cast<float>(point.Y);
			pointer.Down = true;
			pointer.Valid = true;
			return pointer;
		}
	}

	const Mouse& mouse = context.GetMouse();
	pointer.X = static_cast<float>(mouse.GetX());
	pointer.Y = static_cast<float>(mouse.GetY());
	pointer.Down = mouse.IsDown(EMouseButton::Left);
	pointer.Valid = true;
	return pointer;
}

CGameObject* CButton2DSystem::FindHitButton(CGameCanvas& canvas, const Pointer& pointer)
{
	// 레이어마다 역투영이 다르므로(화면 투영 / 패럴랙스 적용 카메라 뷰) 레이어당 1회만 푼다.
	// 버튼 수만큼 푸는 게 아니라 레이어 수만큼이다.
	const std::size_t layerCount = canvas.GetLayerCount();

	HitCandidate best;
	bool hasBest = false;

	canvas.ForEach<Button2D>([&](Button2D& button)
	{
		if (false == button.IsEnabled() || false == button.Interactable)
		{
			return;   // 끈 버튼은 **통과한다** — 뒤의 버튼이 대신 먹는다.
		}
		CGameObject* owner = button.GetOwner().TryGet();
		if (nullptr == owner || false == owner->IsActiveInHierarchy())
		{
			return;
		}
		const CGameLayer* layer = owner->GetLayer().TryGet();
		if (nullptr == layer || false == layer->Visible)
		{
			return;   // 안 보이는 레이어는 안 눌린다.
		}

		Vector2 pointInLayer;
		if (false == canvas.ScreenToLayer(*layer, pointer.X, pointer.Y, pointInLayer))
		{
			return;
		}
		if (false == HitTest(*owner, button, pointInLayer))
		{
			return;
		}

		HitCandidate candidate;
		candidate.Object        = owner;
		candidate.ScreenSpace   = (ELayerSpace::Screen == layer->Space);
		candidate.LayerIndex    = canvas.GetLayerIndex(layer);
		candidate.SortOrder     = BorrowSortOrder(*owner);
		candidate.CreationOrder = owner->GetCreationOrder();

		if (false == hasBest || IsAbove(candidate, best))
		{
			best = candidate;
			hasBest = true;
		}
	});

	(void)layerCount;
	return hasBest ? best.Object : nullptr;
}

void CButton2DSystem::DispatchEnter(CGameCanvas& canvas, CGameObject& object)
{
	JBRO_DISPATCH_BUTTON_HOOK(ButtonEnter);
}

void CButton2DSystem::DispatchExit(CGameCanvas& canvas, CGameObject& object)
{
	JBRO_DISPATCH_BUTTON_HOOK(ButtonExit);
}

void CButton2DSystem::DispatchDown(CGameCanvas& canvas, CGameObject& object)
{
	JBRO_DISPATCH_BUTTON_HOOK(ButtonDown);
}

void CButton2DSystem::DispatchUp(CGameCanvas& canvas, CGameObject& object)
{
	JBRO_DISPATCH_BUTTON_HOOK(ButtonUp);
}

void CButton2DSystem::DispatchClick(CGameCanvas& canvas, CGameObject& object)
{
	JBRO_DISPATCH_BUTTON_HOOK(ButtonClick);
}

#undef JBRO_DISPATCH_BUTTON_HOOK

void CButton2DSystem::Update(CGameCanvas& canvas)
{
	const Pointer pointer = ReadPointer();

	CGameObject* hit = pointer.Valid ? FindHitButton(canvas, pointer) : nullptr;
	m_pointerOverButton = (nullptr != hit);

	// ── 호버 전이 — 경계를 넘을 때만 1회씩 ────────────────────────────────────
	CGameObject* previousHovered = m_hoveredObject.TryGet();
	if (previousHovered != hit)
	{
		if (nullptr != previousHovered)
		{
			if (Button2D* button = previousHovered->GetComponent<Button2D>())
			{
				button->m_hovered = false;
			}
			DispatchExit(canvas, *previousHovered);
		}
		if (nullptr != hit)
		{
			if (Button2D* button = hit->GetComponent<Button2D>())
			{
				button->m_hovered = true;
			}
			DispatchEnter(canvas, *hit);
		}
		m_hoveredObject = (nullptr != hit) ? hit->SafeFromThis() : nullptr;
	}

	// ── 누름/뗌 ───────────────────────────────────────────────────────────────
	const bool downNow = pointer.Valid && pointer.Down;
	const bool justPressed  = downNow && false == m_pointerWasDown;
	const bool justReleased = false == downNow && m_pointerWasDown;
	m_pointerWasDown = downNow;

	if (justPressed && nullptr != hit)
	{
		if (Button2D* button = hit->GetComponent<Button2D>())
		{
			button->m_pressed = true;
		}
		m_pressedObject = hit->SafeFromThis();
		DispatchDown(canvas, *hit);
	}

	if (justReleased)
	{
		if (CGameObject* pressed = m_pressedObject.TryGet())
		{
			if (Button2D* button = pressed->GetComponent<Button2D>())
			{
				button->m_pressed = false;
			}
			DispatchUp(canvas, *pressed);
			// Down 과 Up 이 **같은** 버튼에서 끝났을 때만 클릭이다.
			// 누른 채로 밖으로 끌고 나가 떼는 건 취소다.
			if (pressed == hit)
			{
				DispatchClick(canvas, *pressed);
			}
		}
		m_pressedObject = nullptr;
	}
}

void CButton2DSystem::ResetState(CGameCanvas& canvas)
{
	// 편집 모드로 돌아왔을 때 눌린/호버된 상태가 남으면 안 된다.
	// 훅은 쏘지 않는다 — 시뮬레이션이 끝난 뒤라 받을 스크립트가 없다.
	canvas.ForEach<Button2D>([](Button2D& button)
	{
		button.m_hovered = false;
		button.m_pressed = false;
	});

	m_pointerOverButton = false;
	m_pointerWasDown    = false;
	m_hoveredObject     = nullptr;
	m_pressedObject     = nullptr;
}
