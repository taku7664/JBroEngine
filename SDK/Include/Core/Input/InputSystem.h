#pragma once

#include "Core/Input/InputDevices.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class IInputHandler;
class IRenderSurface;
class CTaskManager;

// 진동 공유 상태 — 워커 타이머 스레드와 InputSystem 이 공유한다(shared_ptr).
// InputSystem 이 먼저 파괴돼도 타이머가 이 블록만 만지므로 use-after-free 가 없다.
// Generation: 슬롯별 Set 마다 증가 — 깨어난 타이머가 자기 세대가 최신일 때만 정지한다.
struct GamepadVibrationState
{
	std::atomic<float>    TargetLeft[InputDeviceContext::MaxGamepadCount]  = {};
	std::atomic<float>    TargetRight[InputDeviceContext::MaxGamepadCount] = {};
	std::atomic<uint32_t> Generation[InputDeviceContext::MaxGamepadCount]  = {};
};

// ─────────────────────────────────────────────────────────────────────────────
//  CInputSystem — 엔진 내부 입력 관리자 (스크립트 비공개).
//
//  · 모든 디바이스 갱신 + InputDeviceContext 스냅샷 생성 + 레이어 순 핸들러 dispatch.
//  · dispatch 는 프레임당 1회. 윈도우 메시지가 아니라 프레임이 구동한다.
//  · 상태 수집: 윈도우=직접폴링(GetAsyncKeyState), 휠 등 폴링불가 신호=이벤트 누적.
//  · 등록/해제는 CGameScript 수명에 묶여 엔진이 호출(스크립트 개입 0).
//    dispatch 중 등록/해제는 deferred 큐로 모았다가 프레임 끝에 flush.
//  · EngineCore(host) 에만 존재. ScriptCore 로 노출하지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
class CInputSystem
{
public:
	void Initialize();
	void Shutdown();

	// 호스트가 메인 surface 를 주입(커서 좌표 폴링용). 전역 Engine 의존을 피해 DLL 링크 안전.
	void SetMainSurface(IRenderSurface* surface) { m_mainSurface = surface; }

	// 호스트가 TaskManager 를 주입(진동 타이머용 워커 스레드). 전역 Engine 의존 회피.
	void SetTaskManager(CTaskManager* taskManager) { m_taskManager = taskManager; }

	// 게임 뷰포트 활성 여부(에디터). false 면 surface 포커스가 있어도 게임 입력 폴링/디스패치를 스킵한다.
	// 에디터: GameView 패널 포커스 시에만 true(인스펙터 등 다른 패널 편집 중 입력 누출 방지).
	// 스탠드얼론 게임: GameView 가 없어 호출되지 않음 → 기본 true 유지(윈도우 포커스만으로 게이팅).
	void SetViewportActive(bool active) { m_viewportActive = active; }

	// 게임 표면(플레이어가 보는 화면)이 창 클라이언트 좌표계의 어디에 어떤 크기로 그려지는지와,
	// 그 표면의 픽셀 해상도. 마우스 좌표를 **게임 표면 픽셀 기준**으로 주기 위해 필요하다.
	//   · 에디터: 게임뷰 패널 안의 레터박스 렉트(패널 위치·비율 보정) × RT 해상도.
	//   · 패키지: 창 클라이언트 전체 × 렌더 타깃 해상도(창 크기 ≠ 해상도면 스케일이 걸린다).
	// 설정하지 않으면 항등(창 좌표 = 표면 좌표)으로 둔다.
	// 게임 스크립트에 창 좌표는 의미가 없다 — 마우스로 월드를 집으려면(Canvas::ScreenToWorld)
	// 렌더 타깃과 같은 좌표계여야 한다. 에디터 UI 는 ImGui 자체 입력을 쓰므로 영향받지 않는다.
	void SetGameSurfaceRect(
		float clientX, float clientY, float clientWidth, float clientHeight,
		float surfaceWidth, float surfaceHeight)
	{
		m_gameSurfaceClientX      = clientX;
		m_gameSurfaceClientY      = clientY;
		m_gameSurfaceClientWidth  = clientWidth;
		m_gameSurfaceClientHeight = clientHeight;
		m_gameSurfaceWidth        = surfaceWidth;
		m_gameSurfaceHeight       = surfaceHeight;
		m_hasGameSurfaceRect      = clientWidth > 0.0f && clientHeight > 0.0f
			&& surfaceWidth > 0.0f && surfaceHeight > 0.0f;
	}

	// 매 프레임 호출. surfaceFocused 면 디바이스 갱신 + dispatch. 아니면 디바이스 클리어.
	void Update(bool surfaceFocused);

	// 등록/해제 — 엔진(CGameScript 수명)이 호출. dispatch 중이면 deferred.
	void RegisterHandler(IInputHandler* handler);
	void UnregisterHandler(IInputHandler* handler);

	// 전역 설정 — 특정 디바이스 입력 무시.
	void SetDeviceEnabled(EInputDevice device, bool enabled);
	bool IsDeviceEnabled(EInputDevice device) const;

	// ── 게임패드 ──────────────────────────────────────────────────────────────
	static constexpr std::size_t MaxGamepadCount = InputDeviceContext::MaxGamepadCount; // 4 (XInput 슬롯)

	// 연결 관리(멀티 패드). 슬롯 인덱스 0..3 = 플레이어 단위.
	int  GetConnectedGamepadCount() const;
	bool IsGamepadConnected(std::size_t index) const;

	// 진동(rumble) — 출력. left/right 모터 강도 0..1.
	// durationSeconds > 0 이면 TaskManager 워커 스레드가 타이머를 재고 시간 후 정지한다.
	//   → 게임 로직(메인 스레드)이 버그로 멈춰도 워커가 모터를 끈다(무한 진동 방지).
	//   워커 미지원(웹) 시 메인 스레드 만료 시각 폴백.
	// durationSeconds <= 0 이면 수동 정지/포커스 상실까지 지속.
	// 포커스 상실/Shutdown 시 항상 자동 정지.
	void SetGamepadVibration(std::size_t index, float leftMotor, float rightMotor, float durationSeconds = 0.0f);
	void StopGamepadVibration(std::size_t index);
	void StopAllVibration();

	// 감도 설정 — 전 패드 공통. deadzone/threshold 는 정규화(0..1).
	void SetStickDeadzone(float deadzone);     // 기본 0.24 (XInput 권장 ~7849/32767)
	void SetTriggerThreshold(float threshold); // 기본 0.12 (~30/255)
	float GetStickDeadzone() const     { return m_stickDeadzone; }
	float GetTriggerThreshold() const  { return m_triggerThreshold; }

	// 폴링 불가 신호 누적(휠 등). WndProc 등 플랫폼 코드가 프레임 사이 호출.
	void AccumulateWheel(float delta);

	// 텍스트 입력 누적(완성 유니코드 코드포인트). WM_CHAR / web keypress 가 프레임 사이 호출.
	// (서로게이트 결합 등 인코딩 처리는 호출측 플랫폼 코드 책임 — 여기는 완성 코드포인트만 받는다.)
	void AccumulateText(char32_t codepoint);

	// 터치 누적(멀티터치) — 모바일 native inject / 웹 콜백이 프레임 사이 호출.
	// id 로 손가락을 추적: Began=슬롯 점유, Moved=좌표 갱신, Ended/Cancelled=슬롯 해제.
	// 좌표는 surface(클라이언트) 픽셀. 메인 스레드 호출 가정(휠/텍스트와 동일).
	void AccumulateTouch(std::int32_t pointerId, int x, int y, ETouchPhase phase);

	// 웹 키/마우스 상태 — emscripten keydown/keyup/mouse 콜백이 프레임 사이 호출한다.
	// 윈도우의 직접 폴링(GetAsyncKeyState/GetCursorPos)에 대응하는 웹 백엔드.
	// down=true 는 눌림, false 는 떼짐. 좌표는 캔버스(클라이언트) 픽셀.
	void AccumulateKey(EKeyCode key, bool down);
	void AccumulateMouseButton(EMouseButton button, bool down);
	void AccumulateMousePosition(int x, int y);

	// 레이어 우선순위 구성(프로젝트 세팅 주입). front = 최우선. 미설정 레이어는 최하위 + 1회 경고.
	void ConfigureLayers(const std::vector<std::string>& orderedLayers);

	// 입력 액션 맵 주입(프로젝트 세팅). 매 프레임 평가되어 ctx.GetAction() 으로 노출된다.
	// 최대 ActionState::MaxActions 개까지(초과분 무시 + 1회 경고).
	void SetInputMap(const std::vector<InputActionDef>& actions);

	// 이번 프레임 스냅샷을 직접 읽는다. 핸들러 dispatch 는 레이어 우선순위 순서라
	// **정해진 시점에 돌아야 하는 엔진 단계**(예: 버튼 판정 — 스크립트 Update 직전)는
	// 그 체인에 낄 수 없다. 그런 단계만 이 접근자를 쓴다.
	const InputDeviceContext& GetDeviceContext() const { return m_context; }

private:
	struct HandlerEntry
	{
		IInputHandler* Handler       = nullptr;
		int            LayerPriority = 0;   // 작을수록 먼저
		int            Order         = 0;   // 클수록 먼저
		std::uint64_t  Seq           = 0;   // 등록순 tiebreak
	};

	int  LayerPriorityOf(const char* layer);
	void FlushPending();
	void SortHandlers();
	void AdvanceFrame();   // current → previous
	void PollDevices();    // 플랫폼 백엔드로 current 채움
	void PollGamepads();   // XInput / 웹 Gamepad API — 멀티 패드 폴링 + 핫플러그 + 진동 적용
	void ApplyVibration(std::size_t index); // 메인: 목표값 변경 시 하드웨어 적용 + 웹 만료 폴백
	void HaltVibrationHardware(); // 전 슬롯 모터 즉시 0(하드웨어 직접) — 포커스 상실/Shutdown
	void ClearDevices();   // 포커스 상실 시 전부 0
	void EvaluateActions(); // 바인딩 + 디바이스 상태 → 액션 값 계산(ctx 액션 스토어 채움)
	void Dispatch();

private:
	InputDeviceContext        m_context;
	std::vector<HandlerEntry> m_handlers;
	std::vector<std::string>  m_layerOrder;
	std::vector<InputActionDef> m_inputMap; // 프로젝트세팅 액션 맵(매 프레임 평가)

	bool m_deviceEnabled[static_cast<std::size_t>(EInputDevice::Count)] = { true, true, true, true };

	// deferred 등록/해제
	std::vector<IInputHandler*> m_pendingRegister;
	std::vector<IInputHandler*> m_pendingUnregister;
	bool          m_inDispatch    = false;
	bool          m_handlersDirty = false;
	std::uint64_t m_nextSeq       = 1;

	std::unordered_set<std::string> m_warnedLayers;

	float           m_accumWheel  = 0.0f;

	// 텍스트 입력 누적 — 프레임 사이 플랫폼이 채우고 PollDevices 가 스냅샷으로 옮긴 뒤 비운다.
	char32_t        m_accumText[Keyboard::MaxTextLength] = {};
	int             m_accumTextLen = 0;

	// 터치 작업 버퍼 — 프레임을 넘겨 유지되는 활성 손가락 상태(id 추적). 프레임마다 스냅샷으로 복사.
	// (휠/텍스트는 프레임 transient 지만 터치는 손가락이 떼질 때까지 활성 유지 → 별도 영속 버퍼.)
	TouchPoint      m_workingTouches[Touch::MaxTouchCount] = {};

	// 창 클라이언트 좌표 → 게임 표면 픽셀. 렉트가 설정되지 않았으면 항등이다.
	// 표면 밖이면 음수/초과 값이 그대로 나온다 — 그걸 받는 쪽(ScreenToWorld)이 "밖"으로 판정한다.
	void ToGameSurfacePoint(int& x, int& y) const
	{
		if (false == m_hasGameSurfaceRect)
		{
			return;
		}
		const float localX = (static_cast<float>(x) - m_gameSurfaceClientX) / m_gameSurfaceClientWidth;
		const float localY = (static_cast<float>(y) - m_gameSurfaceClientY) / m_gameSurfaceClientHeight;
		x = static_cast<int>(localX * m_gameSurfaceWidth);
		y = static_cast<int>(localY * m_gameSurfaceHeight);
	}

	// 게임 표면 렉트(창 클라이언트 좌표) + 표면 해상도 — SetGameSurfaceRect 가 채운다.
	// 마우스를 창 좌표에서 표면 픽셀로 옮기는 데 쓴다(미설정이면 항등).
	float           m_gameSurfaceClientX      = 0.0f;
	float           m_gameSurfaceClientY      = 0.0f;
	float           m_gameSurfaceClientWidth  = 0.0f;
	float           m_gameSurfaceClientHeight = 0.0f;
	float           m_gameSurfaceWidth        = 0.0f;
	float           m_gameSurfaceHeight       = 0.0f;
	bool            m_hasGameSurfaceRect      = false;

	int             m_lastMouseX  = 0;
	int             m_lastMouseY  = 0;

	// 웹 입력 영속 상태 — 콜백(keydown/keyup/mouse)이 채우고 PollDevices 가 컨텍스트로 복사한다.
	// (윈도우는 매 프레임 직접 폴링하므로 이 버퍼가 필요 없다. 웹은 이벤트 구동이라 상태 유지 필요.)
	bool            m_webKeyState[static_cast<std::size_t>(EKeyCode::Count)] = {};
	bool            m_webMouseButton[static_cast<std::size_t>(EMouseButton::Count)] = {};
	int             m_webMouseX     = 0;
	int             m_webMouseY     = 0;

	bool            m_hadFocus      = false;
	bool            m_viewportActive = true; // 에디터 GameView 게이트(스탠드얼론은 항상 true)
	IRenderSurface* m_mainSurface   = nullptr;

	// ── 게임패드 상태/설정 ────────────────────────────────────────────────────
	float m_stickDeadzone    = 0.24f;
	float m_triggerThreshold = 0.12f;

	// 진동 — 목표값/세대는 워커 타이머와 공유(shared_ptr, 수명 안전). 적용값은 메인 전용.
	std::shared_ptr<GamepadVibrationState> m_vibration;
	float m_vibAppliedLeft[MaxGamepadCount]  = {}; // 메인 전용: 마지막 하드웨어 적용값
	float m_vibAppliedRight[MaxGamepadCount] = {};

	// 워커 미지원(웹) 폴백 — 메인 스레드 만료 시각으로 정지.
	bool                                  m_vibHasExpiry[MaxGamepadCount] = {};
	std::chrono::steady_clock::time_point m_vibExpiry[MaxGamepadCount]    = {};

	// 웹 진동 재발행 시각 — 브라우저 effect 가 유한하므로 지속 진동을 주기 재발행한다(ApplyVibration).
	std::chrono::steady_clock::time_point m_webNextReissue[MaxGamepadCount] = {};

	// 핫플러그 — 미연결 슬롯 매 프레임 폴링은 비싸다(XInput 권장). 카운트다운 후 재확인.
	int  m_gamepadRecheck[MaxGamepadCount] = {};

	CTaskManager* m_taskManager = nullptr;
};
