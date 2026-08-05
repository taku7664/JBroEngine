#include "pch.h"
#include "AudioBusProbeScript.h"

#include <cstdio>
#include <string>

namespace
{
	// 헬퍼 이름은 BusLog — 엔진의 Core/Logging 에 class Log 가 있어 Log 는 이름 충돌한다.
	void BusLog(const std::string& message)
	{
		if (Script.Debug)
		{
			Script.Debug->Log("[Audio Bus] " + message);
		}
	}

	// 소수 둘째 자리까지 — 로그에서 0.00 / 1.00 만 구분되면 충분하다.
	std::string FormatVolume(float volume)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%.2f", volume);
		return buffer;
	}
}

void CAudioBusProbeScript::OnStart()
{
	if (false == Script.Audio.IsValid())
	{
		BusLog("audio device is unavailable - nothing to probe.");
		return;
	}
	// 버스 목록은 프로젝트 세팅이 정한다 — 디바이스에 물어봐야 지금 무엇이 있는지 안다.
	m_busNames = Script.Audio->GetBusNames();
	if (m_busNames.empty())
	{
		BusLog("no buses are configured - check Project Settings > Audio.");
		return;
	}

	std::string keys = "ready - 0=reset all,";
	for (std::size_t i = 0; i < m_busNames.size() && i < kMaxProbeKeys; ++i)
	{
		keys += " " + std::to_string(i + 1) + "=" + m_busNames[i];
	}
	BusLog(keys);
	LogAllBuses("start");
}

bool CAudioBusProbeScript::HandleInput(const InputDeviceContext& ctx)
{
	const Keyboard& keyboard = ctx.GetKeyboard();

	if (keyboard.IsPressed(EKeyCode::Num0))
	{
		ResetAllBuses();
		return true;
	}

	// Num1..Num9 는 EKeyCode 에서 연속이라 인덱스로 훑는다.
	const std::size_t count = m_busNames.size() < kMaxProbeKeys ? m_busNames.size() : kMaxProbeKeys;
	for (std::size_t i = 0; i < count; ++i)
	{
		const EKeyCode key = static_cast<EKeyCode>(
			static_cast<std::uint16_t>(EKeyCode::Num1) + static_cast<std::uint16_t>(i));
		if (keyboard.IsPressed(key))
		{
			ToggleBus(m_busNames[i]);
			return true;
		}
	}
	return false;
}

void CAudioBusProbeScript::ToggleBus(const std::string& name)
{
	if (false == Script.Audio.IsValid())
	{
		BusLog("audio device is unavailable.");
		return;
	}

	SafePtr<IAudioBus> bus = Script.Audio->GetBus(name.c_str());
	if (false == bus.IsValid())
	{
		// 조용히 넘어가면 "눌렀는데 아무 일도 없다" 가 되어 원인 파악이 안 된다.
		BusLog(name + " bus is unavailable.");
		return;
	}

	const float next = (bus->GetVolume() > 0.0f) ? 0.0f : 1.0f;
	bus->SetVolume(next);
	// 요청한 이름과 실제로 잡힌 버스 이름을 같이 찍는다 — 목록에 없는 이름이면
	// 디바이스가 Master 로 떨구므로 둘이 달라지고, 그게 바로 진단이 된다.
	BusLog(name + " -> " + FormatVolume(bus->GetVolume())
		+ " (bus=" + bus->GetName() + ")");
}

void CAudioBusProbeScript::ResetAllBuses()
{
	if (false == Script.Audio.IsValid())
	{
		BusLog("audio device is unavailable.");
		return;
	}
	for (const std::string& name : m_busNames)
	{
		if (SafePtr<IAudioBus> bus = Script.Audio->GetBus(name.c_str()))
		{
			bus->SetVolume(1.0f);
		}
	}
	LogAllBuses("reset");
}

void CAudioBusProbeScript::LogAllBuses(const char* reason)
{
	std::string line = std::string("volumes (") + reason + "):";
	for (const std::string& name : m_busNames)
	{
		SafePtr<IAudioBus> bus = Script.Audio.IsValid() ? Script.Audio->GetBus(name.c_str()) : SafePtr<IAudioBus>();
		line += " " + name + "=";
		line += bus.IsValid() ? FormatVolume(bus->GetVolume()) : std::string("n/a");
	}
	BusLog(line);
}
