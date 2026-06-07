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
	Count
};

enum class EMouseButton : std::uint8_t
{
	Left,
	Right,
	Middle,
	Count
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
