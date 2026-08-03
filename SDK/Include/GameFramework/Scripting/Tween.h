#pragma once

#include "Core/ScriptCore.h"
#include "Core/Time/Time.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Scripting/Coroutine.h"
#include "GameFramework/Scripting/Ease.h"
#include "Utillity/Math/Vector2T.h"
#include "Utillity/Pointer/SafePtr.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Tween — 시간에 따라 값을 굴리는 코루틴들.
//
//  별도 트윈 매니저를 두지 않는다. 트윈은 "매 프레임 값을 바꾸며 일정 시간 기다리는 일"인데,
//  그건 코루틴이 이미 하는 일이다. 매니저를 만들면 수명·정지·취소 규칙을 **두 벌** 갖게 되고
//  둘이 어긋나는 순간 죽은 오브젝트를 만지게 된다. 코루틴에 얹으면 기존 계약을 그대로 물려받는다:
//    · 소유 스크립트가 죽으면 트윈도 취소된다(StopCoroutinesForOwner).
//    · 시뮬레이션을 멈추면 트윈도 멈춘다(스케줄러가 재생 중에만 tick).
//    · 프레임 할당이 없다 — 술어·세터가 코루틴 프레임 안에 값으로 산다.
//
//  사용:
//      StartCoroutine(Tween::MoveTo(GetOwner(), Vector2(5, 0), 0.4f, Ease::OutCubic));
//
//      // 임의의 값
//      co_await Tween::Value(0.0f, 1.0f, 0.25f, Ease::Linear,
//          [this](float v) { GetComponent<SpriteRenderer2D>()->Color[3] = v; });
//
//  ⚠ 세터가 캡처한 대상의 수명은 호출자 책임이다. 오브젝트를 만진다면 SafePtr 로 캡처해
//    유효성을 확인하거나, 그 트윈이 소유 스크립트와 함께 취소된다는 점에 기댈 것.
//    아래 MoveTo/ScaleTo 는 SafePtr 를 받아 **매 프레임 확인하고 죽었으면 중단**한다.
// ─────────────────────────────────────────────────────────────────────────────
namespace Tween
{
	// 진행도 0..1 을 duration 초에 걸쳐 세터에 흘려준다.
	// setter 는 값으로 받는다 — 코루틴 프레임 안에 복사돼 살아야 한다(참조로 받으면 호출자
	// 스택의 임시 람다를 가리킨 채 첫 중단에서 댕글링된다).
	template<typename TSetter>
	Coroutine Progress(float duration, Ease::Func ease, TSetter setter)
	{
		const Ease::Func curve = ease ? ease : Ease::Linear;

		// duration <= 0 은 "즉시 끝" 이다. 0 으로 나누지 않으려는 방어가 아니라 정의다.
		if (duration <= 0.0f)
		{
			setter(curve(1.0f));
			co_return;
		}

		float elapsed = 0.0f;
		while (elapsed < duration)
		{
			setter(curve(elapsed / duration));
			co_await Wait::NextFrame();
			elapsed += Script.Time.IsValid() ? Script.Time->GetDeltaSeconds() : 0.0f;
		}

		// 끝값을 정확히 한 번 더 찍는다. 프레임 경계 때문에 마지막 반복이 0.97 에서 끝나면
		// 목표에 도달하지 못한 채 멈춘 것처럼 보인다.
		setter(curve(1.0f));
	}

	// from → to 를 duration 초에 걸쳐 보간해 세터에 넘긴다.
	template<typename TValue, typename TSetter>
	Coroutine Value(TValue from, TValue to, float duration, Ease::Func ease, TSetter setter)
	{
		co_await Progress(duration, ease,
			[from, to, setter](float t) mutable { setter(from + (to - from) * t); });
	}

	// 오브젝트를 현재 위치에서 target 으로 옮긴다.
	// 시작 위치는 **코루틴이 처음 도는 시점**에 읽는다 — 호출 시점이 아니다. 트윈을 큐에 넣고
	// 나중에 시작하는 경우, 그 사이 오브젝트가 움직였다면 지금 자리에서 출발하는 것이 맞다.
	inline Coroutine MoveTo(SafePtr<CGameObject> object, Vector2 target, float duration, Ease::Func ease)
	{
		CGameObject* start = object.TryGet();
		if (nullptr == start)
		{
			co_return;
		}
		const Vector2 from = start->GetTransform().Position;

		co_await Progress(duration, ease, [object, from, target](float t)
		{
			// 매 프레임 확인한다 — 트윈 도중 오브젝트가 파괴될 수 있다.
			if (CGameObject* current = object.TryGet())
			{
				current->GetTransform().Position = from + (target - from) * t;
			}
		});
	}

	// 오브젝트의 로컬 스케일을 target 으로 옮긴다. 시작값 규칙은 MoveTo 와 같다.
	inline Coroutine ScaleTo(SafePtr<CGameObject> object, Vector2 target, float duration, Ease::Func ease)
	{
		CGameObject* start = object.TryGet();
		if (nullptr == start)
		{
			co_return;
		}
		const Vector2 from = start->GetTransform().Scale;

		co_await Progress(duration, ease, [object, from, target](float t)
		{
			if (CGameObject* current = object.TryGet())
			{
				current->GetTransform().Scale = from + (target - from) * t;
			}
		});
	}
}
