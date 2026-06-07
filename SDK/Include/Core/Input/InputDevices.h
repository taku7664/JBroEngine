#pragma once

#include "Core/Input/InputTypes.h"

#include <cstddef>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
//  InputDevice — 입력 장치별 "프레임 입력 스냅샷".
//
//  · 각 디바이스는 prev + current 상태를 보관한다(엣지 IsPressed/IsReleased 계산용).
//  · 모두 고정크기 POD(불리언/정수/실수 고정배열) — 호스트↔게임 DLL 경계 안전.
//    (std::string/STL 금지. 멀티터치/텍스트는 고정배열로.)
//  · 상태를 채우는 건 InputSystem(friend) 뿐. 스크립트/엔진은 읽기 메서드만 쓴다.
//  · DLL 에서 읽기 메서드는 inline 으로 컴파일되며, 레이아웃이 고정이라 안전.
// ─────────────────────────────────────────────────────────────────────────────

class CInputSystem;

enum class EInputDevice : std::uint8_t
{
	Keyboard,
	Mouse,
	Gamepad,
	Touch,
	Count
};

// ── Keyboard ──────────────────────────────────────────────────────────────────
class Keyboard
{
public:
	bool IsDown(EKeyCode key) const     { return Current(key); }
	bool IsPressed(EKeyCode key) const  { return Current(key) && false == Previous(key); }
	bool IsReleased(EKeyCode key) const { return false == Current(key) && Previous(key); }

private:
	static std::size_t Index(EKeyCode key)
	{
		const std::size_t index = static_cast<std::size_t>(key);
		return index < static_cast<std::size_t>(EKeyCode::Count) ? index : 0;
	}
	bool Current(EKeyCode key) const  { return m_current[Index(key)]; }
	bool Previous(EKeyCode key) const { return m_previous[Index(key)]; }

private:
	bool m_current[static_cast<std::size_t>(EKeyCode::Count)]  = {};
	bool m_previous[static_cast<std::size_t>(EKeyCode::Count)] = {};

	friend class CInputSystem;
};

// ── Mouse ─────────────────────────────────────────────────────────────────────
class Mouse
{
public:
	bool IsDown(EMouseButton button) const     { return Current(button); }
	bool IsPressed(EMouseButton button) const  { return Current(button) && false == Previous(button); }
	bool IsReleased(EMouseButton button) const { return false == Current(button) && Previous(button); }

	// 클라이언트 좌표(픽셀). 절대 위치.
	int GetX() const { return m_x; }
	int GetY() const { return m_y; }

	// 지난 프레임 대비 이동량.
	int GetDeltaX() const { return m_deltaX; }
	int GetDeltaY() const { return m_deltaY; }

	// 지난 프레임 사이 누적 휠 델타(노치 단위).
	float GetWheelDelta() const { return m_wheelDelta; }

private:
	static std::size_t Index(EMouseButton button)
	{
		const std::size_t index = static_cast<std::size_t>(button);
		return index < static_cast<std::size_t>(EMouseButton::Count) ? index : 0;
	}
	bool Current(EMouseButton button) const  { return m_current[Index(button)]; }
	bool Previous(EMouseButton button) const { return m_previous[Index(button)]; }

private:
	bool  m_current[static_cast<std::size_t>(EMouseButton::Count)]  = {};
	bool  m_previous[static_cast<std::size_t>(EMouseButton::Count)] = {};
	int   m_x          = 0;
	int   m_y          = 0;
	int   m_deltaX     = 0;
	int   m_deltaY     = 0;
	float m_wheelDelta = 0.0f;

	friend class CInputSystem;
};

// ── Gamepad ───────────────────────────────────────────────────────────────────
class Gamepad
{
public:
	bool IsConnected() const { return m_connected; }

	bool IsDown(EGamepadButton button) const     { return Current(button); }
	bool IsPressed(EGamepadButton button) const  { return Current(button) && false == Previous(button); }
	bool IsReleased(EGamepadButton button) const { return false == Current(button) && Previous(button); }

	// 축 값(-1..1, 트리거는 0..1). deadzone 은 향후 적용.
	float GetAxis(EGamepadAxis axis) const { return m_axes[AxisIndex(axis)]; }

private:
	static std::size_t Index(EGamepadButton button)
	{
		const std::size_t index = static_cast<std::size_t>(button);
		return index < static_cast<std::size_t>(EGamepadButton::Count) ? index : 0;
	}
	static std::size_t AxisIndex(EGamepadAxis axis)
	{
		const std::size_t index = static_cast<std::size_t>(axis);
		return index < static_cast<std::size_t>(EGamepadAxis::Count) ? index : 0;
	}
	bool Current(EGamepadButton button) const  { return m_current[Index(button)]; }
	bool Previous(EGamepadButton button) const { return m_previous[Index(button)]; }

private:
	bool  m_current[static_cast<std::size_t>(EGamepadButton::Count)]  = {};
	bool  m_previous[static_cast<std::size_t>(EGamepadButton::Count)] = {};
	float m_axes[static_cast<std::size_t>(EGamepadAxis::Count)]       = {};
	bool  m_connected = false;

	friend class CInputSystem;
};

// ── Touch ─────────────────────────────────────────────────────────────────────
// 멀티터치 — 고정배열(POD). 동적 컨테이너 금지.
struct TouchPoint
{
	std::int32_t Id  = -1;   // 플랫폼이 부여한 터치 식별자(-1=비활성)
	int          X   = 0;
	int          Y   = 0;
	bool         Active = false;
};

class Touch
{
public:
	static constexpr std::size_t MaxTouchCount = 10;

	int               GetCount() const             { return m_count; }
	const TouchPoint& Get(int index) const         { return m_points[ClampIndex(index)]; }

private:
	static std::size_t ClampIndex(int index)
	{
		if (index < 0) { return 0; }
		const std::size_t i = static_cast<std::size_t>(index);
		return i < MaxTouchCount ? i : MaxTouchCount - 1;
	}

private:
	TouchPoint m_points[MaxTouchCount] = {};
	int        m_count = 0;

	friend class CInputSystem;
};

// ── InputDeviceContext ────────────────────────────────────────────────────────
// 핸들러에 const ref 로 전달되는 디바이스 묶음(host 소유).
// 복사/이동/대입 금지 + 생성자 private(InputSystem 만 생성) → 값 저장 원천봉쇄.
// (주소를 멤버로 들고 다음 프레임에 쓰는 건 C++ 로 못 막는다 — 그 프레임에 다 읽을 것.)
class InputDeviceContext
{
public:
	static constexpr std::size_t MaxGamepadCount = 4;

	InputDeviceContext(const InputDeviceContext&)            = delete;
	InputDeviceContext& operator=(const InputDeviceContext&) = delete;
	InputDeviceContext(InputDeviceContext&&)                 = delete;
	InputDeviceContext& operator=(InputDeviceContext&&)      = delete;

	const Keyboard& GetKeyboard() const { return m_keyboard; }
	const Mouse&    GetMouse() const    { return m_mouse; }
	const Touch&    GetTouch() const    { return m_touch; }
	const Gamepad&  GetGamepad(std::size_t index) const
	{
		return m_gamepads[index < MaxGamepadCount ? index : 0];
	}

private:
	InputDeviceContext() = default;

private:
	Keyboard m_keyboard;
	Mouse    m_mouse;
	Touch    m_touch;
	Gamepad  m_gamepads[MaxGamepadCount];

	friend class CInputSystem;
};
