#include "pch.h"
#include "Scripts/ButtonProbeScript.h"

#include "Core/Input/Input.h"
#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Canvas/GameLayer.h"
#include "GameFramework/Component/Button2D.h"
#include "GameFramework/Component/SpriteRenderer2D.h"

#include <cstddef>
#include <string>

namespace
{
	// 주입 터치 손가락 id — 하나만 쓴다(포인터는 1개라는 계약).
	constexpr std::int32_t PROBE_TOUCH_ID = 7001;

	void ProbeLog(const std::string& message)
	{
		if (Script.Debug)
		{
			Script.Debug->Log("[ButtonProbe] " + message);
		}
	}
}

void CButtonProbeScript::Check(bool condition, const char* label)
{
	if (condition)
	{
		++m_passCount;
		ProbeLog(std::string("PASS  ") + label);
		return;
	}
	++m_failCount;
	ProbeLog(std::string("FAIL  ") + label);
}

void CButtonProbeScript::TearDown()
{
	SafePtr<CGameCanvas> canvas = GetCanvas();
	if (false == canvas.IsValid())
	{
		return;
	}
	for (SafePtr<CGameObject>& objectRef : m_spawned)
	{
		if (CGameObject* object = objectRef.TryGet())
		{
			canvas->DestroyGameObject(object);
		}
	}
	m_spawned.clear();
	if (CGameLayer* layer = m_screenLayer.TryGet())
	{
		canvas->DestroyLayer(layer);
	}
	m_screenLayer = nullptr;
}

void CButtonProbeScript::OnStart()
{
	if (m_started)
	{
		return;
	}
	m_started = true;
	StartCoroutine(RunProbe());
}

Coroutine CButtonProbeScript::RunProbe()
{
	ProbeLog("START");
	ProbeLog("Button2D layout (dll): sizeof=" + std::to_string(sizeof(Button2D))
		+ " offsetSize=" + std::to_string(offsetof(Button2D, Size))
		+ " offsetInteractable=" + std::to_string(offsetof(Button2D, Interactable)));

	SafePtr<CGameCanvas> canvas = GetCanvas();

	// 렌더 크기는 호스트가 렌더 경로에서 채운다 — OnStart 첫 프레임엔 아직 0 이다.
	co_await Wait::Frames(1);

	const float renderWidth  = canvas->GetLastRenderWidth();
	const float renderHeight = canvas->GetLastRenderHeight();
	if (renderWidth < 1.0f || renderHeight < 1.0f)
	{
		ProbeLog("ABORT — no render surface.");
		ProbeLog("RESULT " + std::to_string(m_passCount) + "/"
			+ std::to_string(m_passCount + m_failCount) + " PASS");
		co_return;
	}

	const float centerX = renderWidth * 0.5f;
	const float centerY = renderHeight * 0.5f;

	// ── 준비 ─────────────────────────────────────────────────────────────────
	CGameLayer* screenLayer = canvas->CreateLayer("ButtonProbe.UI");
	Check(nullptr != screenLayer, "screen layer created");
	if (nullptr == screenLayer)
	{
		ProbeLog("RESULT " + std::to_string(m_passCount) + "/"
			+ std::to_string(m_passCount + m_failCount) + " PASS");
		co_return;
	}
	m_screenLayer = screenLayer->SafeFromThis();
	screenLayer->Space = ELayerSpace::Screen;

	CGameLayer* worldLayer = canvas->GetDefaultLayer();

	// 화면 중앙 픽셀이 각 레이어에서 어디인지 물어 **그 자리에** 버튼을 놓는다.
	// (레이어별 역투영 자체는 ScreenSpaceProbe 소관이다. 여기서 보는 건 버튼 쪽이다.)
	Vector2 screenPoint(0.0f, 0.0f);
	Vector2 worldPoint(0.0f, 0.0f);
	Check(canvas->ScreenToLayer(*screenLayer, centerX, centerY, screenPoint), "screen layer un-projects the centre pixel");
	Check(canvas->ScreenToLayer(*worldLayer, centerX, centerY, worldPoint), "world layer un-projects the centre pixel");

	// **컴포넌트 포인터를 들고 다니지 않는다.** 컴포넌트는 타입별 풀에 살고, 뒤이어 같은
	// 타입을 더 추가하면 풀이 커지면서 주소가 옮겨간다 — 먼저 받아 둔 포인터는 옛 자리를
	// 가리키게 되고, 그쪽 값은 엔진이 갱신하지 않는다(첫 실행에서 실제로 이 함정에 빠졌다).
	// 오브젝트만 붙잡고 읽을 때마다 다시 찾는다.
	auto spawnButton = [&](const char* name, CGameLayer& layer, const Vector2& position,
	                       const Vector2& size, std::int32_t sortOrder) -> SafePtr<CGameObject>
	{
		CGameObject* object = canvas->CreateGameObject(name);
		if (nullptr == object)
		{
			return nullptr;
		}
		object->Local.Position = position;
		canvas->MoveObjectToLayer(*object, layer);
		m_spawned.push_back(object->SafeFromThis());

		// 히트 순서는 같은 오브젝트의 렌더러 SortOrder 를 빌려 쓴다 — 렌더러를 붙여야 의미가 있다.
		if (SpriteRenderer2D* renderer = object->AddComponent<SpriteRenderer2D>())
		{
			renderer->SortOrder = sortOrder;
		}
		if (Button2D* button = object->AddComponent<Button2D>())
		{
			button->Size = size;
		}
		return object->SafeFromThis();
	};

	SafePtr<CGameObject> screenObject = spawnButton("Probe.ScreenButton", *screenLayer, screenPoint, Vector2(4.0f, 4.0f), 0);
	SafePtr<CGameObject> worldObject  = spawnButton("Probe.WorldButton",  *worldLayer,  worldPoint,  Vector2(4.0f, 4.0f), 0);
	SafePtr<CGameObject> lowObject    = spawnButton("Probe.OverlapLow",   *worldLayer,  worldPoint,  Vector2(4.0f, 4.0f), 1);
	SafePtr<CGameObject> highObject   = spawnButton("Probe.OverlapHigh",  *worldLayer,  worldPoint,  Vector2(4.0f, 4.0f), 9);

	// 읽을 때마다 다시 찾는다(위 주석의 이유).
	auto buttonOf = [](SafePtr<CGameObject>& objectRef) -> Button2D*
	{
		CGameObject* object = objectRef.TryGet();
		return (nullptr != object) ? object->GetComponent<Button2D>() : nullptr;
	};

	Check(nullptr != buttonOf(screenObject) && nullptr != buttonOf(worldObject)
		&& nullptr != buttonOf(lowObject) && nullptr != buttonOf(highObject), "probe buttons created");
	if (nullptr == buttonOf(screenObject) || nullptr == buttonOf(worldObject)
		|| nullptr == buttonOf(lowObject) || nullptr == buttonOf(highObject))
	{
		TearDown();
		ProbeLog("RESULT " + std::to_string(m_passCount) + "/"
			+ std::to_string(m_passCount + m_failCount) + " PASS");
		co_return;
	}

	// 트랜스폼 전파(월드 행렬)는 시스템 단계다 — 판정이 읽기 전에 한 프레임 넘긴다.
	co_await Wait::Frames(1);

	auto pointerAt = [&](float x, float y, bool down)
	{
		if (Script.Input)
		{
			Script.Input->InjectTouch(PROBE_TOUCH_ID, static_cast<int>(x), static_cast<int>(y),
				down ? ETouchPhase::Moved : ETouchPhase::Ended);
		}
	};
	auto pointerDown = [&](float x, float y)
	{
		if (Script.Input)
		{
			Script.Input->InjectTouch(PROBE_TOUCH_ID, static_cast<int>(x), static_cast<int>(y), ETouchPhase::Began);
		}
	};

	// ── 1) 스페이스가 레이어 인덱스보다 먼저다 ───────────────────────────────
	// 같은 픽셀에 화면 버튼과 월드 버튼이 겹쳐 있다. 화면 레이어는 라이팅 뒤에 합성되므로
	// 화면 버튼이 위다 — 여기서 월드 버튼이 먹으면 "보이는 건 UI 인데 클릭은 뒤가 먹는" 상태다.
	pointerDown(centerX, centerY);
	co_await Wait::Frames(2);
	Check(buttonOf(screenObject)->IsHovered(), "screen-layer button takes the pointer");
	Check(false == buttonOf(worldObject)->IsHovered(), "world-layer button underneath does not");
	Check(canvas->IsPointerOverButton(), "IsPointerOverButton reports the hit");

	// ── 2) 눌림 상태 ─────────────────────────────────────────────────────────
	Check(buttonOf(screenObject)->IsPressed(), "pointer down latches pressed");

	// ── 3) 판정 렉트는 대상 그래픽과 무관하다 ← 이번 개편의 핵심 ──────────────
	// 전이를 켜고 대상 그래픽의 크기를 크게 바꿔도 눌리는 자리는 그대로여야 한다.
	// (구 설계는 여기서 렉트가 따라 움직여 호버가 발진했다.)
	//
	// ⚠ 아래 Size/Offset 비교는 **약한 검사**다 — 이 스크립트가 쓴 값을 이 스크립트가 되읽는다.
	// "렉트가 안 변했다"는 보지만 "그 렉트로 판정이 실제로 유지된다"는 못 본다.
	// 그 실증은 바로 뒤의 IsHovered 검사다 — 그래픽 모양을 크게 흔든 뒤에도 여전히 호버다.
	{
		CGameObject* owner = screenObject.TryGet();
		SpriteRenderer2D* graphic = (nullptr != owner) ? owner->GetComponent<SpriteRenderer2D>() : nullptr;
		Check(nullptr != graphic, "target graphic exists");
		if (nullptr != graphic)
		{
			const Vector2 sizeBefore   = buttonOf(screenObject)->Size;
			const Vector2 offsetBefore = buttonOf(screenObject)->Offset;

			buttonOf(screenObject)->TargetGraphic.SetComponentGuid(owner->GetInstanceGuid(), graphic->GetInstanceGuid());
			buttonOf(screenObject)->Transition = EButtonTransition::SpriteSwap;
			graphic->SetSize(Vector2(12.0f, 0.1f));   // 그리는 모양을 크게 흔든다
			co_await Wait::Frames(2);

			Check(buttonOf(screenObject)->Size.x == sizeBefore.x && buttonOf(screenObject)->Size.y == sizeBefore.y,
				"[weak] hit size field unchanged after a target-graphic change");
			Check(buttonOf(screenObject)->Offset.x == offsetBefore.x && buttonOf(screenObject)->Offset.y == offsetBefore.y,
				"[weak] hit offset field unchanged after a target-graphic change");
			Check(buttonOf(screenObject)->IsHovered(), "still hovered after the graphic changed shape");
		}
	}

	// ── 4) 경계를 넘으면 호버가 풀린다 ───────────────────────────────────────
	pointerAt(0.0f, 0.0f, true);
	co_await Wait::Frames(2);
	Check(false == buttonOf(screenObject)->IsHovered(), "pointer off the rect clears hover");
	Check(false == canvas->IsPointerOverButton(), "IsPointerOverButton clears too");

	// ── 5) 겹친 버튼은 위쪽만 먹는다 ─────────────────────────────────────────
	// 화면 버튼을 비활성으로 만들어 월드 레이어 경쟁을 드러낸다.
	buttonOf(screenObject)->SetEnabled(false);
	pointerAt(centerX, centerY, true);
	co_await Wait::Frames(2);
	Check(buttonOf(highObject)->IsHovered(), "higher sort order wins the overlap");
	Check(false == buttonOf(lowObject)->IsHovered(), "lower sort order does not");

	// ── 6) Interactable=false 는 가리지 않고 통과시킨다 ──────────────────────
	buttonOf(highObject)->Interactable = false;
	co_await Wait::Frames(2);
	Check(false == buttonOf(highObject)->IsHovered(), "non-interactable button is not hovered");
	Check(buttonOf(lowObject)->IsHovered(), "the pointer falls through to the button behind");

	// 손가락을 뗀다.
	pointerAt(centerX, centerY, false);
	co_await Wait::Frames(2);

	// ── 7) 훅 발화 ───────────────────────────────────────────────────────────
	// 훅은 버튼과 **같은 오브젝트**의 스크립트로 간다. 런타임 AddScript 는 공개돼 있지
	// 않으므로 프로브 자신의 오브젝트를 버튼으로 만든다 — 그러면 아래 카운터가 곧 발화다.
	// 화면 레이어에 두면 아직 살아 있는 월드 버튼들보다 무조건 위다(스페이스가 먼저).
	if (CGameObject* self = GetOwner().TryGet())
	{
		SafePtr<CGameLayer> previousLayer = self->GetLayer();
		const Vector2 previousPosition = self->Local.Position;

		self->Local.Position = screenPoint;
		canvas->MoveObjectToLayer(*self, *screenLayer);
		Button2D* selfButton = self->AddComponent<Button2D>();
		Check(nullptr != selfButton, "probe object became a button");
		if (nullptr != selfButton)
		{
			selfButton->Size = Vector2(4.0f, 4.0f);
			co_await Wait::Frames(1);

			// 눌렀다 같은 자리에서 뗀다 → Enter/Down/Up/Click 이 한 번씩.
			pointerDown(centerX, centerY);
			co_await Wait::Frames(2);
			Check(1 == m_enterCount, "OnButtonEnter fires once on entry");
			Check(1 == m_downCount,  "OnButtonDown fires on press");
			Check(0 == m_clickCount, "OnButtonClick does not fire before release");

			const int enterAfterPress = m_enterCount;
			co_await Wait::Frames(2);
			Check(enterAfterPress == m_enterCount, "OnButtonEnter does not repeat while held inside");

			pointerAt(centerX, centerY, false);
			co_await Wait::Frames(2);
			Check(1 == m_upCount,    "OnButtonUp fires on release");
			Check(1 == m_clickCount, "OnButtonClick fires when press and release share the button");

			// 누른 채로 밖으로 끌고 나가 떼면 취소다 — Up 은 오지만 Click 은 안 온다.
			pointerDown(centerX, centerY);
			co_await Wait::Frames(2);
			pointerAt(0.0f, 0.0f, true);
			co_await Wait::Frames(2);
			Check(m_exitCount >= 1, "OnButtonExit fires when the pointer leaves");
			pointerAt(0.0f, 0.0f, false);
			co_await Wait::Frames(2);
			Check(2 == m_upCount,    "OnButtonUp fires again on the second release");
			Check(1 == m_clickCount, "OnButtonClick does not fire when the release lands outside");
		}

		// 프로브 오브젝트는 캔버스가 저작한 것이다 — 빌린 것을 그대로 돌려놓는다.
		// (아래 TearDown 이 화면 레이어를 지우므로 여기서 안 빼면 같이 파괴된다.)
		self->RemoveComponent<Button2D>();
		self->Local.Position = previousPosition;
		if (CGameLayer* restored = previousLayer.TryGet())
		{
			canvas->MoveObjectToLayer(*self, *restored);
		}
	}

	TearDown();
	ProbeLog("RESULT " + std::to_string(m_passCount) + "/"
		+ std::to_string(m_passCount + m_failCount) + " PASS");
}
