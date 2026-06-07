#include "pch.h"
#include "InputSystem.h"

#include "Core/Input/IInputHandler.h"
#include "Core/Logging/LoggerInternal.h"
#include "Core/Platform/IRenderSurface.h"
#include "Core/Platform/RenderSurfaceTypes.h"
#include "Core/Task/TaskManager.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <thread>

namespace
{
	// 미연결 게임패드 슬롯 재확인 주기(프레임). XInput 미연결 슬롯 폴링은 비싸 throttle.
	constexpr int kGamepadRecheckFrames = 120;

	// 정규화 스틱(-1..1, 라디얼 deadzone) — (rawX, rawY) → (outX, outY).
	void NormalizeStick(float rawX, float rawY, float deadzone, float& outX, float& outY)
	{
		const float magnitude = std::sqrt(rawX * rawX + rawY * rawY);
		if (magnitude <= deadzone || magnitude <= 0.0f)
		{
			outX = 0.0f;
			outY = 0.0f;
			return;
		}
		const float clamped = magnitude > 1.0f ? 1.0f : magnitude;
		const float scaled   = (clamped - deadzone) / (1.0f - deadzone); // deadzone..1 → 0..1
		const float scale    = scaled / magnitude;
		outX = rawX * scale;
		outY = rawY * scale;
	}

	// 정규화 트리거(0..1, threshold 적용).
	float NormalizeTrigger(float raw, float threshold)
	{
		if (raw <= threshold)
		{
			return 0.0f;
		}
		return (raw - threshold) / (1.0f - threshold);
	}
}

#if JBRO_PLATFORM_WINDOWS
#include <windows.h>
#include <Xinput.h>
#pragma comment(lib, "Xinput9_1_0.lib") // 윈도우 기본 탑재 — 재배포 DLL 불필요

namespace
{
	// EKeyCode → 가상키 코드(VK). 현재 키 테이블 기준(Phase 2 에서 키 보강 시 함께 확장).
	int ToVirtualKey(EKeyCode key)
	{
		if (key >= EKeyCode::A && key <= EKeyCode::Z)
		{
			return 'A' + (static_cast<int>(key) - static_cast<int>(EKeyCode::A));
		}
		if (key >= EKeyCode::Num0 && key <= EKeyCode::Num9)
		{
			return '0' + (static_cast<int>(key) - static_cast<int>(EKeyCode::Num0));
		}

		switch (key)
		{
		case EKeyCode::Escape:    return VK_ESCAPE;
		case EKeyCode::Space:     return VK_SPACE;
		case EKeyCode::Enter:     return VK_RETURN;
		case EKeyCode::Tab:       return VK_TAB;
		case EKeyCode::Backspace: return VK_BACK;
		case EKeyCode::Left:      return VK_LEFT;
		case EKeyCode::Right:     return VK_RIGHT;
		case EKeyCode::Up:        return VK_UP;
		case EKeyCode::Down:      return VK_DOWN;
		default:                  return 0;
		}
	}

	bool IsVirtualKeyDown(int vk)
	{
		if (0 == vk)
		{
			return false;
		}
		return 0 != (GetAsyncKeyState(vk) & 0x8000);
	}
}
#endif

#if JBRO_PLATFORM_WEB
#include <emscripten/html5.h>
#endif

void CInputSystem::Initialize()
{
	// 기본 레이어 우선순위(프로젝트 세팅이 ConfigureLayers 로 덮어쓸 수 있음).
	// front = 최우선. 미설정 레이어는 최하위 + 1회 경고.
	m_layerOrder = { "Modal", "UI", "Game", "World", "Debug" };

	// 진동 공유 상태 — 워커 타이머와 공유(InputSystem 보다 오래 살 수 있음).
	m_vibration = std::make_shared<GamepadVibrationState>();
}

void CInputSystem::Shutdown()
{
	HaltVibrationHardware(); // 종료 시 모터 정지(스턱 진동 방지).
	m_handlers.clear();
	m_pendingRegister.clear();
	m_pendingUnregister.clear();
	m_warnedLayers.clear();
}

void CInputSystem::ConfigureLayers(const std::vector<std::string>& orderedLayers)
{
	if (false == orderedLayers.empty())
	{
		m_layerOrder = orderedLayers;
	}
	m_warnedLayers.clear();
	m_handlersDirty = true;
}

int CInputSystem::LayerPriorityOf(const char* layer)
{
	const std::string name = (nullptr != layer) ? layer : "Game";
	for (std::size_t i = 0; i < m_layerOrder.size(); ++i)
	{
		if (m_layerOrder[i] == name)
		{
			return static_cast<int>(i);
		}
	}

	// 미설정 레이어 → 최하위 배치 + 1회 경고.
	if (m_warnedLayers.insert(name).second)
	{
		CSystemLog::Warning(std::format(
			"InputSystem: unknown input layer \"{}\" — falling back to lowest priority. "
			"Add it to project input layer settings.", name));
	}
	return static_cast<int>(m_layerOrder.size());
}

void CInputSystem::RegisterHandler(IInputHandler* handler)
{
	if (nullptr == handler)
	{
		return;
	}
	if (m_inDispatch)
	{
		m_pendingRegister.push_back(handler);
		return;
	}

	for (const HandlerEntry& entry : m_handlers)
	{
		if (entry.Handler == handler)
		{
			return; // 중복 등록 방지
		}
	}

	HandlerEntry entry;
	entry.Handler       = handler;
	entry.LayerPriority = LayerPriorityOf(handler->GetInputLayer());
	entry.Order         = handler->GetInputOrder();
	entry.Seq           = m_nextSeq++;
	m_handlers.push_back(entry);
	m_handlersDirty = true;

	// [진단] 등록 확인용 로그(라이브컴파일이라 디버거 심볼이 안 붙는 상황 대비).
	CSystemLog::Info(std::format("InputSystem: handler registered layer=\"{}\" order={} total={}",
		handler->GetInputLayer(), entry.Order, m_handlers.size()));
}

void CInputSystem::UnregisterHandler(IInputHandler* handler)
{
	if (nullptr == handler)
	{
		return;
	}
	if (m_inDispatch)
	{
		m_pendingUnregister.push_back(handler);
		return;
	}

	m_handlers.erase(
		std::remove_if(m_handlers.begin(), m_handlers.end(),
			[handler](const HandlerEntry& entry) { return entry.Handler == handler; }),
		m_handlers.end());
}

void CInputSystem::FlushPending()
{
	for (IInputHandler* handler : m_pendingUnregister)
	{
		UnregisterHandler(handler);
	}
	m_pendingUnregister.clear();

	for (IInputHandler* handler : m_pendingRegister)
	{
		RegisterHandler(handler);
	}
	m_pendingRegister.clear();
}

void CInputSystem::SortHandlers()
{
	std::sort(m_handlers.begin(), m_handlers.end(),
		[](const HandlerEntry& lhs, const HandlerEntry& rhs)
		{
			if (lhs.LayerPriority != rhs.LayerPriority)
			{
				return lhs.LayerPriority < rhs.LayerPriority; // 작을수록 먼저
			}
			if (lhs.Order != rhs.Order)
			{
				return lhs.Order > rhs.Order;                 // 클수록 먼저
			}
			return lhs.Seq < rhs.Seq;                         // 등록순
		});
	m_handlersDirty = false;
}

void CInputSystem::SetDeviceEnabled(EInputDevice device, bool enabled)
{
	const std::size_t index = static_cast<std::size_t>(device);
	if (index < static_cast<std::size_t>(EInputDevice::Count))
	{
		m_deviceEnabled[index] = enabled;
	}
}

bool CInputSystem::IsDeviceEnabled(EInputDevice device) const
{
	const std::size_t index = static_cast<std::size_t>(device);
	return index < static_cast<std::size_t>(EInputDevice::Count) ? m_deviceEnabled[index] : false;
}

void CInputSystem::AccumulateWheel(float delta)
{
	m_accumWheel += delta;
}

int CInputSystem::GetConnectedGamepadCount() const
{
	int count = 0;
	for (std::size_t i = 0; i < InputDeviceContext::MaxGamepadCount; ++i)
	{
		if (m_context.GetGamepad(i).IsConnected())
		{
			++count;
		}
	}
	return count;
}

bool CInputSystem::IsGamepadConnected(std::size_t index) const
{
	if (index >= InputDeviceContext::MaxGamepadCount)
	{
		return false;
	}
	return m_context.GetGamepad(index).IsConnected();
}

void CInputSystem::SetGamepadVibration(std::size_t index, float leftMotor, float rightMotor, float durationSeconds)
{
	if (index >= MaxGamepadCount || !m_vibration)
	{
		return;
	}
	const float left  = std::clamp(leftMotor, 0.0f, 1.0f);
	const float right = std::clamp(rightMotor, 0.0f, 1.0f);

	// 새 명령 — 세대 증가(기존 타이머 무효화). 목표값 저장(메인 PollGamepads 가 하드웨어에 반영).
	const uint32_t generation = m_vibration->Generation[index].fetch_add(1) + 1;
	m_vibration->TargetLeft[index].store(left);
	m_vibration->TargetRight[index].store(right);
	m_vibHasExpiry[index] = false;

	if (durationSeconds <= 0.0f)
	{
		return; // 무한 — 수동 정지/포커스 상실까지 지속
	}

	const auto micros = std::chrono::microseconds(static_cast<long long>(durationSeconds * 1'000'000.0f));

	// 타이머 — 워커 스레드가 시간 후 정지. 메인 스레드(게임 로직)가 버그로 멈춰도 동작한다.
	// shared_ptr 캡처라 InputSystem 이 먼저 파괴돼도 안전(use-after-free 없음). gen 으로 스테일 취소.
	if (m_taskManager && m_taskManager->IsWorkerThreadSupported())
	{
		std::shared_ptr<GamepadVibrationState> vib = m_vibration;
		const std::size_t slot = index;
		m_taskManager->CreateTask("GamepadRumbleTimer", [vib, slot, generation, micros]()
		{
			std::this_thread::sleep_for(micros);
			if (vib->Generation[slot].load() != generation)
			{
				return; // 더 새로운 진동이 슬롯을 점유 — 무시
			}
			vib->TargetLeft[slot].store(0.0f);
			vib->TargetRight[slot].store(0.0f);
#if JBRO_PLATFORM_WINDOWS
			// 메인 스레드가 멈춰도 모터를 끈다(하드웨어 직접). XInputSetState 는 스레드 안전.
			XINPUT_VIBRATION off = {};
			XInputSetState(static_cast<DWORD>(slot), &off);
#endif
		}, "패드 진동 타이머");
	}
	else
	{
		// 워커 미지원(웹 단일 스레드) — 메인 스레드 만료 시각 폴백.
		m_vibExpiry[index]    = std::chrono::steady_clock::now() + micros;
		m_vibHasExpiry[index] = true;
	}
}

void CInputSystem::StopGamepadVibration(std::size_t index)
{
	SetGamepadVibration(index, 0.0f, 0.0f, 0.0f); // 세대 증가로 진행 중 타이머도 취소
}

void CInputSystem::StopAllVibration()
{
	for (std::size_t i = 0; i < MaxGamepadCount; ++i)
	{
		StopGamepadVibration(i);
	}
}

void CInputSystem::ApplyVibration(std::size_t index)
{
	if (!m_vibration)
	{
		return;
	}

	// 웹 폴백 만료 체크(메인 스레드). 워커 경로면 m_vibHasExpiry 는 항상 false.
	if (m_vibHasExpiry[index] && std::chrono::steady_clock::now() >= m_vibExpiry[index])
	{
		m_vibHasExpiry[index] = false;
		m_vibration->Generation[index].fetch_add(1);
		m_vibration->TargetLeft[index].store(0.0f);
		m_vibration->TargetRight[index].store(0.0f);
	}

	const float left  = m_vibration->TargetLeft[index].load();
	const float right = m_vibration->TargetRight[index].load();
	if (left == m_vibAppliedLeft[index] && right == m_vibAppliedRight[index])
	{
		return; // 변경 없음 — XInputSetState 생략
	}
	m_vibAppliedLeft[index]  = left;
	m_vibAppliedRight[index] = right;
#if JBRO_PLATFORM_WINDOWS
	XINPUT_VIBRATION vibration = {};
	vibration.wLeftMotorSpeed  = static_cast<WORD>(left  * 65535.0f);
	vibration.wRightMotorSpeed = static_cast<WORD>(right * 65535.0f);
	XInputSetState(static_cast<DWORD>(index), &vibration);
#endif
}

void CInputSystem::SetStickDeadzone(float deadzone)
{
	m_stickDeadzone = std::clamp(deadzone, 0.0f, 0.95f);
}

void CInputSystem::SetTriggerThreshold(float threshold)
{
	m_triggerThreshold = std::clamp(threshold, 0.0f, 0.95f);
}

void CInputSystem::AdvanceFrame()
{
	std::copy(std::begin(m_context.m_keyboard.m_current), std::end(m_context.m_keyboard.m_current),
		std::begin(m_context.m_keyboard.m_previous));
	std::copy(std::begin(m_context.m_mouse.m_current), std::end(m_context.m_mouse.m_current),
		std::begin(m_context.m_mouse.m_previous));
	for (Gamepad& pad : m_context.m_gamepads)
	{
		std::copy(std::begin(pad.m_current), std::end(pad.m_current), std::begin(pad.m_previous));
	}
}

void CInputSystem::PollDevices()
{
	const bool keyboardEnabled = IsDeviceEnabled(EInputDevice::Keyboard);
	const bool mouseEnabled    = IsDeviceEnabled(EInputDevice::Mouse);

#if JBRO_PLATFORM_WINDOWS
	// 키보드 — 직접 폴링.
	for (std::size_t i = 0; i < static_cast<std::size_t>(EKeyCode::Count); ++i)
	{
		const EKeyCode key = static_cast<EKeyCode>(i);
		m_context.m_keyboard.m_current[i] = keyboardEnabled ? IsVirtualKeyDown(ToVirtualKey(key)) : false;
	}

	// 마우스 버튼 — 직접 폴링.
	m_context.m_mouse.m_current[static_cast<std::size_t>(EMouseButton::Left)]   = mouseEnabled && IsVirtualKeyDown(VK_LBUTTON);
	m_context.m_mouse.m_current[static_cast<std::size_t>(EMouseButton::Right)]  = mouseEnabled && IsVirtualKeyDown(VK_RBUTTON);
	m_context.m_mouse.m_current[static_cast<std::size_t>(EMouseButton::Middle)] = mouseEnabled && IsVirtualKeyDown(VK_MBUTTON);

	// 마우스 위치 — 클라이언트 좌표.
	int newX = m_lastMouseX;
	int newY = m_lastMouseY;
	if (mouseEnabled && nullptr != m_mainSurface)
	{
		const NativeSurfaceHandle native = m_mainSurface->GetNativeSurfaceHandle();
		if (ERenderSurfaceType::Win32Hwnd == native.SurfaceType && nullptr != native.Handle)
		{
			POINT cursor = {};
			if (GetCursorPos(&cursor) && ScreenToClient(static_cast<HWND>(native.Handle), &cursor))
			{
				newX = cursor.x;
				newY = cursor.y;
			}
		}
	}
	m_context.m_mouse.m_x      = newX;
	m_context.m_mouse.m_y      = newY;
	m_context.m_mouse.m_deltaX = newX - m_lastMouseX;
	m_context.m_mouse.m_deltaY = newY - m_lastMouseY;
	m_lastMouseX = newX;
	m_lastMouseY = newY;
#else
	// 웹/모바일 백엔드(이벤트 누적)는 후속 단계에서 구현. 현재는 미갱신(0 유지).
	(void)keyboardEnabled;
	(void)mouseEnabled;
#endif

	// 휠 — 폴링 불가 → 누적값 소비.
	m_context.m_mouse.m_wheelDelta = mouseEnabled ? m_accumWheel : 0.0f;
	m_accumWheel = 0.0f;

	// 게임패드 — 멀티 슬롯 폴링(XInput / 웹). 비활성 시 전 슬롯 0 + 진동 정지.
	if (IsDeviceEnabled(EInputDevice::Gamepad))
	{
		PollGamepads();
	}
	else
	{
		for (std::size_t i = 0; i < InputDeviceContext::MaxGamepadCount; ++i)
		{
			m_context.m_gamepads[i] = Gamepad{};
		}
		StopAllVibration();
	}

	// 터치 백엔드는 후속 단계.
}

void CInputSystem::PollGamepads()
{
#if JBRO_PLATFORM_WINDOWS
	// EGamepadButton → XINPUT 플래그 매핑(인덱스 = enum 값 순서).
	static const WORD kButtonFlags[static_cast<std::size_t>(EGamepadButton::Count)] = {
		XINPUT_GAMEPAD_A,              // South
		XINPUT_GAMEPAD_B,              // East
		XINPUT_GAMEPAD_X,              // West
		XINPUT_GAMEPAD_Y,              // North
		XINPUT_GAMEPAD_LEFT_SHOULDER,  // LeftShoulder
		XINPUT_GAMEPAD_RIGHT_SHOULDER, // RightShoulder
		XINPUT_GAMEPAD_START,          // Start
		XINPUT_GAMEPAD_BACK,           // Select
		XINPUT_GAMEPAD_DPAD_UP,        // DPadUp
		XINPUT_GAMEPAD_DPAD_DOWN,      // DPadDown
		XINPUT_GAMEPAD_DPAD_LEFT,      // DPadLeft
		XINPUT_GAMEPAD_DPAD_RIGHT,     // DPadRight
		XINPUT_GAMEPAD_LEFT_THUMB,     // LeftThumb
		XINPUT_GAMEPAD_RIGHT_THUMB,    // RightThumb
	};

	for (std::size_t i = 0; i < InputDeviceContext::MaxGamepadCount; ++i)
	{
		Gamepad& pad = m_context.m_gamepads[i];

		// 미연결 슬롯은 throttle — 매 프레임 XInputGetState 안 함(빈 슬롯 폴링 비용 큼).
		if (false == pad.m_connected && m_gamepadRecheck[i] > 0)
		{
			--m_gamepadRecheck[i];
			continue;
		}

		XINPUT_STATE state = {};
		const DWORD result = XInputGetState(static_cast<DWORD>(i), &state);
		if (ERROR_SUCCESS != result)
		{
			// 연결 끊김 — 상태 0, 진동 정지, 재확인 throttle.
			const bool wasConnected = pad.m_connected;
			pad = Gamepad{};
			m_gamepadRecheck[i] = kGamepadRecheckFrames;
			if (wasConnected)
			{
				StopGamepadVibration(i);
			}
			continue;
		}

		pad.m_connected = true;

		const WORD buttons = state.Gamepad.wButtons;
		for (std::size_t b = 0; b < static_cast<std::size_t>(EGamepadButton::Count); ++b)
		{
			pad.m_current[b] = 0 != (buttons & kButtonFlags[b]);
		}

		// 스틱 — 정규화(-1..1) + 라디얼 deadzone. (Y 는 위가 +)
		float lx = 0.0f;
		float ly = 0.0f;
		NormalizeStick(state.Gamepad.sThumbLX / 32767.0f, state.Gamepad.sThumbLY / 32767.0f, m_stickDeadzone, lx, ly);
		float rx = 0.0f;
		float ry = 0.0f;
		NormalizeStick(state.Gamepad.sThumbRX / 32767.0f, state.Gamepad.sThumbRY / 32767.0f, m_stickDeadzone, rx, ry);

		pad.m_axes[static_cast<std::size_t>(EGamepadAxis::LeftX)]  = lx;
		pad.m_axes[static_cast<std::size_t>(EGamepadAxis::LeftY)]  = ly;
		pad.m_axes[static_cast<std::size_t>(EGamepadAxis::RightX)] = rx;
		pad.m_axes[static_cast<std::size_t>(EGamepadAxis::RightY)] = ry;
		pad.m_axes[static_cast<std::size_t>(EGamepadAxis::LeftTrigger)]  = NormalizeTrigger(state.Gamepad.bLeftTrigger / 255.0f, m_triggerThreshold);
		pad.m_axes[static_cast<std::size_t>(EGamepadAxis::RightTrigger)] = NormalizeTrigger(state.Gamepad.bRightTrigger / 255.0f, m_triggerThreshold);

		// 진동 — 목표값 변경 시에만 하드웨어 적용(워커 타이머가 끈 것도 여기서 반영).
		ApplyVibration(i);
	}
#elif JBRO_PLATFORM_WEB
	const int count = emscripten_get_num_gamepads();
	for (std::size_t i = 0; i < InputDeviceContext::MaxGamepadCount; ++i)
	{
		Gamepad& pad = m_context.m_gamepads[i];

		EmscriptenGamepadEvent state = {};
		if (static_cast<int>(i) >= count ||
			EMSCRIPTEN_RESULT_SUCCESS != emscripten_get_gamepad_status(static_cast<int>(i), &state) ||
			0 == state.connected)
		{
			pad = Gamepad{};
			continue;
		}

		pad.m_connected = true;

		// 표준 게임패드(W3C) 버튼 인덱스 → EGamepadButton.
		static const int kWebButton[static_cast<std::size_t>(EGamepadButton::Count)] = {
			0,  // South (A)
			1,  // East  (B)
			2,  // West  (X)
			3,  // North (Y)
			4,  // LeftShoulder
			5,  // RightShoulder
			9,  // Start
			8,  // Select (Back)
			12, // DPadUp
			13, // DPadDown
			14, // DPadLeft
			15, // DPadRight
			10, // LeftThumb (L3)
			11, // RightThumb (R3)
		};
		for (std::size_t b = 0; b < static_cast<std::size_t>(EGamepadButton::Count); ++b)
		{
			const int idx = kWebButton[b];
			pad.m_current[b] = (idx < state.numButtons) && (0 != state.digitalButton[idx]);
		}

		const float lxRaw = state.numAxes > 0 ? static_cast<float>(state.axis[0]) : 0.0f;
		const float lyRaw = state.numAxes > 1 ? -static_cast<float>(state.axis[1]) : 0.0f; // 웹은 아래가 + → 뒤집기
		const float rxRaw = state.numAxes > 2 ? static_cast<float>(state.axis[2]) : 0.0f;
		const float ryRaw = state.numAxes > 3 ? -static_cast<float>(state.axis[3]) : 0.0f;

		float lx = 0.0f;
		float ly = 0.0f;
		NormalizeStick(lxRaw, lyRaw, m_stickDeadzone, lx, ly);
		float rx = 0.0f;
		float ry = 0.0f;
		NormalizeStick(rxRaw, ryRaw, m_stickDeadzone, rx, ry);

		pad.m_axes[static_cast<std::size_t>(EGamepadAxis::LeftX)]  = lx;
		pad.m_axes[static_cast<std::size_t>(EGamepadAxis::LeftY)]  = ly;
		pad.m_axes[static_cast<std::size_t>(EGamepadAxis::RightX)] = rx;
		pad.m_axes[static_cast<std::size_t>(EGamepadAxis::RightY)] = ry;
		// 표준 게임패드 트리거 = analogButton[6/7].
		pad.m_axes[static_cast<std::size_t>(EGamepadAxis::LeftTrigger)]  = (6 < state.numButtons) ? NormalizeTrigger(static_cast<float>(state.analogButton[6]), m_triggerThreshold) : 0.0f;
		pad.m_axes[static_cast<std::size_t>(EGamepadAxis::RightTrigger)] = (7 < state.numButtons) ? NormalizeTrigger(static_cast<float>(state.analogButton[7]), m_triggerThreshold) : 0.0f;
		// 웹 진동(Haptic)은 emscripten 표준 경로 미지원 → 후속.
	}
#endif
}

void CInputSystem::HaltVibrationHardware()
{
	for (std::size_t i = 0; i < MaxGamepadCount; ++i)
	{
		if (m_vibration)
		{
			m_vibration->Generation[i].fetch_add(1); // 진행 중 타이머 무효화
			m_vibration->TargetLeft[i].store(0.0f);
			m_vibration->TargetRight[i].store(0.0f);
		}
		m_vibAppliedLeft[i]  = 0.0f;
		m_vibAppliedRight[i] = 0.0f;
		m_vibHasExpiry[i]    = false;
	}
#if JBRO_PLATFORM_WINDOWS
	// 폴링이 멈춘 상태(포커스 상실)에서도 모터가 멈추도록 하드웨어에 직접 0 기록.
	XINPUT_VIBRATION off = {};
	for (DWORD i = 0; i < static_cast<DWORD>(MaxGamepadCount); ++i)
	{
		XInputSetState(i, &off);
	}
#endif
}

void CInputSystem::ClearDevices()
{
	m_context.m_keyboard = Keyboard{};
	m_context.m_mouse    = Mouse{};
	m_context.m_touch    = Touch{};
	for (Gamepad& pad : m_context.m_gamepads)
	{
		pad = Gamepad{};
	}
	m_accumWheel = 0.0f;
}

void CInputSystem::Dispatch()
{
	if (m_handlersDirty)
	{
		SortHandlers();
	}

	// [진단] handlers 가 있을 때 첫 dispatch 1회만 로그(포커스/디스패치 동작 확인).
	static bool s_loggedFirstDispatch = false;
	if (false == s_loggedFirstDispatch && false == m_handlers.empty())
	{
		s_loggedFirstDispatch = true;
		CSystemLog::Info(std::format("InputSystem: first dispatch with {} handler(s) (focused, polling active).",
			m_handlers.size()));
	}

	m_inDispatch = true;
	for (const HandlerEntry& entry : m_handlers)
	{
		if (nullptr == entry.Handler)
		{
			continue;
		}
		if (entry.Handler->HandleInput(m_context))
		{
			break; // consume → 하위 레이어 차단
		}
	}
	m_inDispatch = false;

	FlushPending();
}

void CInputSystem::Update(bool surfaceFocused)
{
	// 등록/해제 큐 반영(dispatch 밖에서 안전).
	FlushPending();

	if (false == surfaceFocused)
	{
		if (m_hadFocus)
		{
			ClearDevices();           // 포커스 상실 → 디바이스 클리어(스턱 방지). dispatch 스킵.
			HaltVibrationHardware();  // 알트탭 중 모터 멈춤(폴링 정지해도 직접 0 기록).
			m_hadFocus = false;
		}
		return;
	}

	if (false == m_hadFocus)
	{
		// 포커스 복귀 — 엣지 오인 방지를 위해 위치 기준 재설정.
		ClearDevices();
		m_hadFocus = true;
	}

	AdvanceFrame();
	PollDevices();
	Dispatch();
}
