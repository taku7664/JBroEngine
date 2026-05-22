#pragma once

#include "Core/Input/IInputMessageHandler.h"
#include "Utillity/SafePtr.h"

#include <array>
#include <vector>

class CInput final : public EnableSafeFromThis<CInput>
{
public:
	CInput() = default;
	~CInput() = default;
	CInput(const CInput&) = delete;
	CInput& operator=(const CInput&) = delete;

public:
	void BeginFrame();
	void EndFrame();

	bool SubmitMessage(const InputMessage& message);
	void RegisterHandler(IInputMessageHandler* handler, int layer);
	void UnregisterHandler(IInputMessageHandler* handler);

	bool IsKeyDown(EKeyCode key) const;
	bool WasKeyPressed(EKeyCode key) const;
	bool WasKeyReleased(EKeyCode key) const;
	bool IsMouseButtonDown(EMouseButton button) const;
	bool WasMouseButtonPressed(EMouseButton button) const;
	bool WasMouseButtonReleased(EMouseButton button) const;
	bool IsGamepadButtonDown(std::uint32_t gamepadIndex, EGamepadButton button) const;
	bool WasGamepadButtonPressed(std::uint32_t gamepadIndex, EGamepadButton button) const;
	bool WasGamepadButtonReleased(std::uint32_t gamepadIndex, EGamepadButton button) const;
	float GetGamepadAxis(std::uint32_t gamepadIndex, EGamepadAxis axis) const;
	int GetMouseX() const;
	int GetMouseY() const;
	float GetMouseWheelDelta() const;

private:
	struct HandlerEntry
	{
		IInputMessageHandler* Handler = nullptr;
		int Layer = 0;
		std::uint64_t Order = 0;
	};

private:
	static constexpr std::size_t MAX_GAMEPAD_COUNT = 4;
	static std::size_t ToKeyIndex(EKeyCode key);
	static std::size_t ToMouseButtonIndex(EMouseButton button);
	static std::size_t ToGamepadIndex(std::uint32_t gamepadIndex);
	static std::size_t ToGamepadButtonIndex(EGamepadButton button);
	static std::size_t ToGamepadAxisIndex(EGamepadAxis axis);
	void ApplyState(const InputMessage& message);
	bool DispatchMessage(const InputMessage& message);
	void SortHandlers();

private:
	std::array<bool, static_cast<std::size_t>(EKeyCode::Count)> m_currentKeys = {};
	std::array<bool, static_cast<std::size_t>(EKeyCode::Count)> m_previousKeys = {};
	std::array<bool, static_cast<std::size_t>(EMouseButton::Count)> m_currentMouseButtons = {};
	std::array<bool, static_cast<std::size_t>(EMouseButton::Count)> m_previousMouseButtons = {};
	std::array<std::array<bool, static_cast<std::size_t>(EGamepadButton::Count)>, MAX_GAMEPAD_COUNT> m_currentGamepadButtons = {};
	std::array<std::array<bool, static_cast<std::size_t>(EGamepadButton::Count)>, MAX_GAMEPAD_COUNT> m_previousGamepadButtons = {};
	std::array<std::array<float, static_cast<std::size_t>(EGamepadAxis::Count)>, MAX_GAMEPAD_COUNT> m_gamepadAxes = {};
	std::vector<HandlerEntry> m_handlers;
	std::uint64_t m_nextOrder = 1;
	int m_mouseX = 0;
	int m_mouseY = 0;
	float m_mouseWheelDelta = 0.0f;
	bool m_handlersDirty = false;
};
