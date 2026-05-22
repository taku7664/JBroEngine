#include "pch.h"
#include "Input.h"

#include <algorithm>

void CInput::BeginFrame()
{
	m_previousKeys = m_currentKeys;
	m_previousMouseButtons = m_currentMouseButtons;
	m_previousGamepadButtons = m_currentGamepadButtons;
	m_mouseWheelDelta = 0.0f;
}

void CInput::EndFrame()
{
}

bool CInput::SubmitMessage(const InputMessage& message)
{
	ApplyState(message);
	return DispatchMessage(message);
}

void CInput::RegisterHandler(IInputMessageHandler* handler, int layer)
{
	if (nullptr == handler)
	{
		return;
	}

	auto it = std::find_if(m_handlers.begin(), m_handlers.end(), [handler](const HandlerEntry& entry) {
		return entry.Handler == handler;
		});
	if (it != m_handlers.end())
	{
		it->Layer = layer;
		m_handlersDirty = true;
		return;
	}

	m_handlers.push_back(HandlerEntry{ handler, layer, m_nextOrder++ });
	m_handlersDirty = true;
}

void CInput::UnregisterHandler(IInputMessageHandler* handler)
{
	m_handlers.erase(std::remove_if(m_handlers.begin(), m_handlers.end(), [handler](const HandlerEntry& entry) {
		return entry.Handler == handler;
		}), m_handlers.end());
}

bool CInput::IsKeyDown(EKeyCode key) const
{
	return m_currentKeys[ToKeyIndex(key)];
}

bool CInput::WasKeyPressed(EKeyCode key) const
{
	const std::size_t index = ToKeyIndex(key);
	return m_currentKeys[index] && false == m_previousKeys[index];
}

bool CInput::WasKeyReleased(EKeyCode key) const
{
	const std::size_t index = ToKeyIndex(key);
	return false == m_currentKeys[index] && m_previousKeys[index];
}

bool CInput::IsMouseButtonDown(EMouseButton button) const
{
	return m_currentMouseButtons[ToMouseButtonIndex(button)];
}

bool CInput::WasMouseButtonPressed(EMouseButton button) const
{
	const std::size_t index = ToMouseButtonIndex(button);
	return m_currentMouseButtons[index] && false == m_previousMouseButtons[index];
}

bool CInput::WasMouseButtonReleased(EMouseButton button) const
{
	const std::size_t index = ToMouseButtonIndex(button);
	return false == m_currentMouseButtons[index] && m_previousMouseButtons[index];
}

bool CInput::IsGamepadButtonDown(std::uint32_t gamepadIndex, EGamepadButton button) const
{
	return m_currentGamepadButtons[ToGamepadIndex(gamepadIndex)][ToGamepadButtonIndex(button)];
}

bool CInput::WasGamepadButtonPressed(std::uint32_t gamepadIndex, EGamepadButton button) const
{
	const std::size_t padIndex = ToGamepadIndex(gamepadIndex);
	const std::size_t buttonIndex = ToGamepadButtonIndex(button);
	return m_currentGamepadButtons[padIndex][buttonIndex] && false == m_previousGamepadButtons[padIndex][buttonIndex];
}

bool CInput::WasGamepadButtonReleased(std::uint32_t gamepadIndex, EGamepadButton button) const
{
	const std::size_t padIndex = ToGamepadIndex(gamepadIndex);
	const std::size_t buttonIndex = ToGamepadButtonIndex(button);
	return false == m_currentGamepadButtons[padIndex][buttonIndex] && m_previousGamepadButtons[padIndex][buttonIndex];
}

float CInput::GetGamepadAxis(std::uint32_t gamepadIndex, EGamepadAxis axis) const
{
	return m_gamepadAxes[ToGamepadIndex(gamepadIndex)][ToGamepadAxisIndex(axis)];
}

int CInput::GetMouseX() const
{
	return m_mouseX;
}

int CInput::GetMouseY() const
{
	return m_mouseY;
}

float CInput::GetMouseWheelDelta() const
{
	return m_mouseWheelDelta;
}

std::size_t CInput::ToKeyIndex(EKeyCode key)
{
	const std::size_t index = static_cast<std::size_t>(key);
	return index < static_cast<std::size_t>(EKeyCode::Count) ? index : 0;
}

std::size_t CInput::ToMouseButtonIndex(EMouseButton button)
{
	const std::size_t index = static_cast<std::size_t>(button);
	return index < static_cast<std::size_t>(EMouseButton::Count) ? index : 0;
}

std::size_t CInput::ToGamepadIndex(std::uint32_t gamepadIndex)
{
	return gamepadIndex < MAX_GAMEPAD_COUNT ? static_cast<std::size_t>(gamepadIndex) : 0;
}

std::size_t CInput::ToGamepadButtonIndex(EGamepadButton button)
{
	const std::size_t index = static_cast<std::size_t>(button);
	return index < static_cast<std::size_t>(EGamepadButton::Count) ? index : 0;
}

std::size_t CInput::ToGamepadAxisIndex(EGamepadAxis axis)
{
	const std::size_t index = static_cast<std::size_t>(axis);
	return index < static_cast<std::size_t>(EGamepadAxis::Count) ? index : 0;
}

void CInput::ApplyState(const InputMessage& message)
{
	switch (message.Type)
	{
	case EInputMessageType::KeyDown:
		m_currentKeys[ToKeyIndex(message.Key)] = true;
		break;
	case EInputMessageType::KeyUp:
		m_currentKeys[ToKeyIndex(message.Key)] = false;
		break;
	case EInputMessageType::MouseDown:
		m_currentMouseButtons[ToMouseButtonIndex(message.MouseButton)] = true;
		break;
	case EInputMessageType::MouseUp:
		m_currentMouseButtons[ToMouseButtonIndex(message.MouseButton)] = false;
		break;
	case EInputMessageType::MouseMove:
		m_mouseX = message.MouseX;
		m_mouseY = message.MouseY;
		break;
	case EInputMessageType::MouseWheel:
		m_mouseWheelDelta += message.WheelDelta;
		break;
	case EInputMessageType::GamepadButtonDown:
		m_currentGamepadButtons[ToGamepadIndex(message.GamepadIndex)][ToGamepadButtonIndex(message.GamepadButton)] = true;
		break;
	case EInputMessageType::GamepadButtonUp:
		m_currentGamepadButtons[ToGamepadIndex(message.GamepadIndex)][ToGamepadButtonIndex(message.GamepadButton)] = false;
		break;
	case EInputMessageType::GamepadAxis:
		m_gamepadAxes[ToGamepadIndex(message.GamepadIndex)][ToGamepadAxisIndex(message.GamepadAxis)] = message.AxisValue;
		break;
	default:
		break;
	}
}

bool CInput::DispatchMessage(const InputMessage& message)
{
	if (m_handlersDirty)
	{
		SortHandlers();
	}

	for (const HandlerEntry& entry : m_handlers)
	{
		if (entry.Handler && false == entry.Handler->OnReceiveInputMessage(message))
		{
			return false;
		}
	}

	return true;
}

void CInput::SortHandlers()
{
	std::sort(m_handlers.begin(), m_handlers.end(), [](const HandlerEntry& lhs, const HandlerEntry& rhs) {
		if (lhs.Layer != rhs.Layer)
		{
			return lhs.Layer > rhs.Layer;
		}
		return lhs.Order > rhs.Order;
		});
	m_handlersDirty = false;
}
