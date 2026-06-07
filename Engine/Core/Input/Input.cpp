#include "pch.h"
#include "Input.h"

#include "Core/Input/InputSystem.h"

void CInput::SetDeviceEnabled(EInputDevice device, bool enabled)
{
	if (m_system)
	{
		m_system->SetDeviceEnabled(device, enabled);
	}
}

bool CInput::IsDeviceEnabled(EInputDevice device) const
{
	return m_system ? m_system->IsDeviceEnabled(device) : false;
}

int CInput::GetConnectedGamepadCount() const
{
	return m_system ? m_system->GetConnectedGamepadCount() : 0;
}

bool CInput::IsGamepadConnected(std::size_t index) const
{
	return m_system ? m_system->IsGamepadConnected(index) : false;
}

void CInput::SetGamepadVibration(std::size_t index, float leftMotor, float rightMotor, float durationSeconds)
{
	if (m_system)
	{
		m_system->SetGamepadVibration(index, leftMotor, rightMotor, durationSeconds);
	}
}

void CInput::StopGamepadVibration(std::size_t index)
{
	if (m_system)
	{
		m_system->StopGamepadVibration(index);
	}
}

void CInput::SetStickDeadzone(float deadzone)
{
	if (m_system)
	{
		m_system->SetStickDeadzone(deadzone);
	}
}

void CInput::SetTriggerThreshold(float threshold)
{
	if (m_system)
	{
		m_system->SetTriggerThreshold(threshold);
	}
}
