#pragma once

#include <cstdint>

enum class EInputMessageType
{
	None,
	KeyDown,
	KeyUp,
	MouseDown,
	MouseUp,
	MouseMove,
	MouseWheel,
	GamepadButtonDown,
	GamepadButtonUp,
	GamepadAxis
};

enum class EKeyCode : std::uint16_t
{
	Unknown = 0,
	Escape,
	Space,
	Enter,
	Tab,
	Backspace,
	Left,
	Right,
	Up,
	Down,
	A, B, C, D, E, F, G, H, I, J, K, L, M,
	N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
	Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

	// ── 모디파이어 (좌/우 구분) ──
	LeftShift, RightShift,
	LeftCtrl,  RightCtrl,
	LeftAlt,   RightAlt,

	// ── 펑션 키 (F1..F12 연속) ──
	F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

	// ── 편집/네비게이션 ──
	Insert, Delete, Home, End, PageUp, PageDown, CapsLock,

	// ── 기호 (US 레이아웃 기준) ──
	Minus,        // -
	Equals,       // =
	LeftBracket,  // [
	RightBracket, // ]
	Backslash,    // backslash
	Semicolon,    // ;
	Apostrophe,   // '
	Comma,        // ,
	Period,       // .
	Slash,        // /
	Grave,        // `

	// ── 숫자패드 (Numpad0..9 연속) ──
	Numpad0, Numpad1, Numpad2, Numpad3, Numpad4,
	Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
	NumpadAdd, NumpadSubtract, NumpadMultiply, NumpadDivide, NumpadDecimal, NumpadEnter,

	Count
};

enum class EMouseButton : std::uint8_t
{
	Left,
	Right,
	Middle,
	Count
};

// 터치 포인터 이벤트 단계 — 플랫폼(모바일 inject / 웹 콜백)이 InputSystem 에 누적할 때 사용.
enum class ETouchPhase : std::uint8_t
{
	Began,      // 새 터치 시작(손가락 닿음)
	Moved,      // 같은 id 이동
	Ended,      // 손가락 뗌
	Cancelled   // 시스템 취소(통화/제스처 가로채기 등)
};

enum class EGamepadButton : std::uint8_t
{
	South,          // Xbox A / DualShock X
	East,           // B / O
	West,           // X / Square
	North,          // Y / Triangle
	LeftShoulder,
	RightShoulder,
	Start,
	Select,         // Back / Share
	DPadUp,
	DPadDown,
	DPadLeft,
	DPadRight,
	LeftThumb,      // 왼쪽 스틱 클릭(L3)
	RightThumb,     // 오른쪽 스틱 클릭(R3)
	Count
};

enum class EGamepadAxis : std::uint8_t
{
	LeftX,
	LeftY,
	RightX,
	RightY,
	LeftTrigger,
	RightTrigger,
	Count
};

struct InputMessage
{
	EInputMessageType Type = EInputMessageType::None;
	EKeyCode Key = EKeyCode::Unknown;
	EMouseButton MouseButton = EMouseButton::Left;
	EGamepadButton GamepadButton = EGamepadButton::South;
	EGamepadAxis GamepadAxis = EGamepadAxis::LeftX;
	std::uint32_t GamepadIndex = 0;
	int MouseX = 0;
	int MouseY = 0;
	float WheelDelta = 0.0f;
	float AxisValue = 0.0f;
};
