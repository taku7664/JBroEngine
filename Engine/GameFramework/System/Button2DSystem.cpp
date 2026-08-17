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

#include <algorithm>
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

	// 버튼은 렌더러가 아니라 자기 SortOrder 가 없다. 판정 순서를 **보이는 순서**에 맞추려면
	// 같은 오브젝트의 렌더러에서 가져와야 하는데, 렌더러가 여럿일 수 있다.
	//
	// **가장 큰 값을 쓴다.** "첫 번째로 찾은 렌더러"는 컴포넌트 추가 순서에 따라 답이 바뀌는
	// 은근한 지목이라, 아이콘 자식을 하나 붙이는 것만으로 판정 순서가 소리 없이 뒤집힌다
	// (Button2D 가 TargetGraphic 으로 대상을 **명시**하게 만든 것과 같은 이유로 금지한다).
	// 최댓값은 순서에 무관하고, 뜻도 맞는다 — 이 오브젝트가 화면에서 다른 것보다 위에 보이는지는
	// 그것이 그리는 것 중 **가장 위**가 정한다.
	//
	// (GetComponent<CRenderer2DComponent> 는 못 쓴다 — 조회가 정확한 타입명 키다.)
	std::int32_t TopmostRendererSortOrder(const CGameObject& object)
	{
		std::int32_t topmost = 0;
		bool found = false;
		for (const SafePtr<CComponent>& component : object.GetComponents())
		{
			if (false == component.IsValid())
			{
				continue;
			}
			const CRenderer2DComponent* renderer = component->AsRenderer2DComponent();
			if (nullptr == renderer)
			{
				continue;
			}
			// 꺼진 렌더러는 안 그려진다 — 안 보이는 것이 순서를 정하면 안 된다.
			if (false == renderer->IsEnabled())
			{
				continue;
			}
			topmost = found ? std::max(topmost, renderer->SortOrder) : renderer->SortOrder;
			found = true;
		}
		return topmost;
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

	// 눈에 보이는 상태. 우선순위는 비활성 > 눌림 > 호버 > 기본이다 —
	// 못 누르는 버튼이 호버로 밝아지면 누를 수 있다고 거짓말하는 셈이다.
	enum class EVisualState { Normal, Hovered, Pressed, Disabled };

	EVisualState VisualStateOf(const Button2D& button)
	{
		if (false == button.Interactable) { return EVisualState::Disabled; }
		if (button.IsPressed())           { return EVisualState::Pressed; }
		if (button.IsHovered())           { return EVisualState::Hovered; }
		return EVisualState::Normal;
	}

	// 훅을 받을 자격 — 애니메이션 디스패치(SpriteAnimationSystem.cpp:111)와 같은 판정이다.
	bool CanReceive(const CGameScript& script)
	{
		const CGameObject* owner = script.GetOwner().TryGet();
		return script.IsEnabled() && nullptr != owner && owner->IsActiveInHierarchy();
	}
}

template <typename Fn>
void CButton2DSystem::DispatchToScripts(CGameCanvas& canvas, CGameObject& object, Fn&& fn)
{
	canvas.ForEachScriptOnObject(object, [&fn](CGameScript& script, CGameCanvas::ScriptRuntimeState&)
	{
		if (CanReceive(script))
		{
			fn(script);
		}
	});
}

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

	// 닿아 있는 손가락이 없다면, 이번 프레임에 **뗀** 손가락이 있는지 본다.
	// 그 자리를 안 쓰고 마우스로 폴백하면 릴리스 판정이 엉뚱한 좌표에서 벌어져
	// "누른 버튼에서 뗐는가"가 항상 거짓이 된다(터치에서 클릭이 영영 안 난다).
	// 활성 손가락을 먼저 보는 순서는 유지한다 — 살아 있는 손가락이 뗀 자리에 밀리면 안 된다.
	for (int index = 0; index < touch.GetCount(); ++index)
	{
		const TouchPoint& point = touch.Get(index);
		if (ETouchPhase::Ended == point.Phase)
		{
			pointer.X = static_cast<float>(point.X);
			pointer.Y = static_cast<float>(point.Y);
			pointer.Down = false;
			pointer.Valid = true;
			return pointer;
		}
		if (ETouchPhase::Cancelled == point.Phase)
		{
			// 시스템이 터치를 가로챈 것이다(통화·제스처). 뗀 것이 아니므로 클릭이 아니다 —
			// 포인터를 무효로 돌려 이번 프레임 히트를 없앤다. Up 은 나가고 Click 은 안 난다.
			return pointer;   // Valid=false
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
	// 레이어마다 역투영이 다르다(화면 투영 / 패럴랙스 적용 카메라 뷰). 그 역투영은 뷰포트 순회 +
	// 카메라 해석이라 싸지 않으므로 **레이어당 1회**만 풀고 재사용한다.
	// 버퍼는 시스템이 들고 프레임마다 다시 쓴다 — 프레임 루프에서 새로 할당하지 않는다.
	const std::size_t layerCount = canvas.GetLayerCount();
	m_layerPointCache.assign(layerCount, LayerPoint{});

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

		const int layerIndex = canvas.GetLayerIndex(layer);
		if (layerIndex < 0 || static_cast<std::size_t>(layerIndex) >= m_layerPointCache.size())
		{
			return;   // 캔버스에 속하지 않은 레이어 — 역투영 기준이 없다.
		}

		LayerPoint& cached = m_layerPointCache[static_cast<std::size_t>(layerIndex)];
		if (false == cached.Resolved)
		{
			// 실패도 결과다 — 못 푸는 레이어를 버튼마다 다시 풀지 않는다.
			cached.Resolved = true;
			cached.Valid    = canvas.ScreenToLayer(*layer, pointer.X, pointer.Y, cached.Point);
		}
		if (false == cached.Valid)
		{
			return;
		}
		if (false == HitTest(*owner, button, cached.Point))
		{
			return;
		}

		HitCandidate candidate;
		candidate.Object        = owner;
		candidate.ScreenSpace   = (ELayerSpace::Screen == layer->Space);
		candidate.LayerIndex    = layerIndex;
		candidate.SortOrder     = TopmostRendererSortOrder(*owner);
		candidate.CreationOrder = owner->GetCreationOrder();

		if (false == hasBest || IsAbove(candidate, best))
		{
			best = candidate;
			hasBest = true;
		}
	});

	return hasBest ? best.Object : nullptr;
}

void CButton2DSystem::ApplyTransition(Button2D& button)
{
	SpriteRenderer2D* target = button.TargetGraphic.Get();
	if (EButtonTransition::None == button.Transition || nullptr == target)
	{
		// 전이를 껐거나 대상이 사라졌다 — 쥐고 있던 게 있으면 놓는다.
		// 대상이 죽은 경우 되돌릴 곳이 없으므로 래치만 푼다(훅은 계속 발화한다).
		ReleaseTransition(button);
		return;
	}

	// 소유 시작 시점의 값이 normal 이다. 여기서 한 번만 뜬다 —
	// 매 프레임 다시 뜨면 우리가 칠한 색을 normal 로 착각한다.
	if (false == button.m_latched)
	{
		button.m_latched          = true;
		button.m_normalColor      = target->Color;
		button.m_normalSpriteGuid = target->GetSpriteGuid();
	}

	const EVisualState state = VisualStateOf(button);

	if (EButtonTransition::ColorTint == button.Transition)
	{
		switch (state)
		{
		case EVisualState::Hovered:  target->Color = button.HoverTint;    break;
		case EVisualState::Pressed:  target->Color = button.PressedTint;  break;
		case EVisualState::Disabled: target->Color = button.DisabledTint; break;
		default:                     target->Color = button.m_normalColor; break;
		}
		return;
	}

	// SpriteSwap — 비운 칸은 normal 유지. 스프라이트 크기가 달라도 판정 렉트는 안 움직인다
	// (렉트가 렌더러와 무관하다는 것이 이 설계의 핵심이다).
	const Ref<CSpriteAsset>* swap = nullptr;
	switch (state)
	{
	case EVisualState::Hovered:  swap = &button.HoverSprite;    break;
	case EVisualState::Pressed:  swap = &button.PressedSprite;  break;
	case EVisualState::Disabled: swap = &button.DisabledSprite; break;
	default: break;
	}

	const AssetGuid desired = (nullptr != swap && static_cast<bool>(*swap))
		? swap->GetGuid()
		: button.m_normalSpriteGuid;
	if (target->GetSpriteGuid() != desired)
	{
		// 세터를 탄다 — 스프라이트가 바뀌면 컬링 경계도 다시 계산돼야 한다.
		target->SetSpriteGuid(desired);
	}
}

void CButton2DSystem::ReleaseTransition(Button2D& button)
{
	if (false == button.m_latched)
	{
		return;
	}
	button.m_latched = false;

	SpriteRenderer2D* target = button.TargetGraphic.Get();
	if (nullptr == target)
	{
		return;   // 대상이 사라졌다 — 되돌릴 곳이 없다.
	}
	target->Color = button.m_normalColor;
	if (target->GetSpriteGuid() != button.m_normalSpriteGuid)
	{
		target->SetSpriteGuid(button.m_normalSpriteGuid);
	}
}

void CButton2DSystem::DispatchEnter(CGameCanvas& canvas, CGameObject& object)
{
	DispatchToScripts(canvas, object, [](CGameScript& script) { script.ButtonEnter(); });
}

void CButton2DSystem::DispatchExit(CGameCanvas& canvas, CGameObject& object)
{
	DispatchToScripts(canvas, object, [](CGameScript& script) { script.ButtonExit(); });
}

void CButton2DSystem::DispatchDown(CGameCanvas& canvas, CGameObject& object)
{
	DispatchToScripts(canvas, object, [](CGameScript& script) { script.ButtonDown(); });
}

void CButton2DSystem::DispatchUp(CGameCanvas& canvas, CGameObject& object)
{
	DispatchToScripts(canvas, object, [](CGameScript& script) { script.ButtonUp(); });
}

void CButton2DSystem::DispatchClick(CGameCanvas& canvas, CGameObject& object)
{
	DispatchToScripts(canvas, object, [](CGameScript& script) { script.ButtonClick(); });
}


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

	// 상태가 확정된 뒤에 칠한다 — 훅과 같은 프레임에 그림이 바뀐다.
	canvas.ForEach<Button2D>([](Button2D& button)
	{
		ApplyTransition(button);
	});
}

void CButton2DSystem::ResetState(CGameCanvas& canvas)
{
	// 편집 모드로 돌아왔을 때 눌린/호버된 상태가 남으면 안 된다.
	// 훅은 쏘지 않는다 — 시뮬레이션이 끝난 뒤라 받을 스크립트가 없다.
	canvas.ForEach<Button2D>([](Button2D& button)
	{
		button.m_hovered = false;
		button.m_pressed = false;
		// 눌린 그림이 편집 모드에 남으면 안 된다 — 게다가 그 값이 그대로 저장될 수 있다.
		ReleaseTransition(button);
	});

	m_pointerOverButton = false;
	m_pointerWasDown    = false;
	m_hoveredObject     = nullptr;
	m_pressedObject     = nullptr;
}
