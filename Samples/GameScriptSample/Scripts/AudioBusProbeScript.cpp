#include "pch.h"
#include "AudioBusProbeScript.h"

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

	// 게임 DLL 에는 magic_enum 이 없다(호스트 전용). 로그용 이름은 손으로 적는다.
	const char* BusName(EAudioBusKind kind)
	{
		switch (kind)
		{
		case EAudioBusKind::Master: return "Master";
		case EAudioBusKind::Music:  return "Music";
		case EAudioBusKind::SFX:    return "SFX";
		case EAudioBusKind::Voice:  return "Voice";
		case EAudioBusKind::UI:     return "UI";
		case EAudioBusKind::Custom: return "Custom";
		}
		return "?";
	}

	// 소수 둘째 자리까지 — 로그에서 0.00 / 1.00 만 구분되면 충분하다.
	std::string FormatVolume(float volume)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%.2f", volume);
		return buffer;
	}

	constexpr EAudioBusKind kProbeBuses[] = {
		EAudioBusKind::Master,
		EAudioBusKind::Music,
		EAudioBusKind::SFX,
		EAudioBusKind::Voice,
		EAudioBusKind::UI,
	};
}

void CAudioBusProbeScript::OnStart()
{
	if (false == Script.Audio.IsValid())
	{
		BusLog("audio device is unavailable - nothing to probe.");
		return;
	}
	BusLog("ready - 1=Master 2=Music 3=SFX 4=Voice 5=UI toggle, 0=reset all to 1.0");
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

	// Num1..Num5 는 EKeyCode 에서 연속이라 인덱스로 훑는다.
	for (std::size_t i = 0; i < std::size(kProbeBuses); ++i)
	{
		const EKeyCode key = static_cast<EKeyCode>(
			static_cast<std::uint16_t>(EKeyCode::Num1) + static_cast<std::uint16_t>(i));
		if (keyboard.IsPressed(key))
		{
			ToggleBus(kProbeBuses[i]);
			return true;
		}
	}
	return false;
}

void CAudioBusProbeScript::ToggleBus(EAudioBusKind kind)
{
	if (false == Script.Audio.IsValid())
	{
		BusLog("audio device is unavailable.");
		return;
	}

	SafePtr<IAudioBus> bus = Script.Audio->GetBus(kind);
	if (false == bus.IsValid())
	{
		// 조용히 넘어가면 "눌렀는데 아무 일도 없다" 가 되어 원인 파악이 안 된다.
		BusLog(std::string(BusName(kind)) + " bus is unavailable.");
		return;
	}

	const float next = (bus->GetVolume() > 0.0f) ? 0.0f : 1.0f;
	bus->SetVolume(next);
	BusLog(std::string(BusName(kind)) + " -> " + FormatVolume(bus->GetVolume()));
}

void CAudioBusProbeScript::ResetAllBuses()
{
	if (false == Script.Audio.IsValid())
	{
		BusLog("audio device is unavailable.");
		return;
	}
	for (const EAudioBusKind kind : kProbeBuses)
	{
		if (SafePtr<IAudioBus> bus = Script.Audio->GetBus(kind))
		{
			bus->SetVolume(1.0f);
		}
	}
	LogAllBuses("reset");
}

void CAudioBusProbeScript::LogAllBuses(const char* reason)
{
	std::string line = std::string("volumes (") + reason + "):";
	for (const EAudioBusKind kind : kProbeBuses)
	{
		SafePtr<IAudioBus> bus = Script.Audio.IsValid() ? Script.Audio->GetBus(kind) : SafePtr<IAudioBus>();
		line += std::string(" ") + BusName(kind) + "=";
		line += bus.IsValid() ? FormatVolume(bus->GetVolume()) : std::string("n/a");
	}
	BusLog(line);
}
