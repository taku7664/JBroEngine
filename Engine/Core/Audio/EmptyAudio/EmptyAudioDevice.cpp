#include "pch.h"
#include "EmptyAudioDevice.h"

bool CEmptyAudioDevice::Initialize(const AudioDeviceDesc&)
{
	m_listener = MakeOwnerPtr<CEmptyAudioListener>();

	// no-op 백엔드라 상태만 보관한다. 프로젝트 목록은 ConfigureBuses 가 주입한다.
	ConfigureBuses({});
	return static_cast<bool>(m_listener);
}

void CEmptyAudioDevice::Finalize()
{
	m_buses.clear();
	m_listener.Reset();
}

void CEmptyAudioDevice::ConfigureBuses(const std::vector<AudioBusDef>& buses)
{
	m_buses.clear();
	// Master 는 목록과 무관하게 항상 첫 번째 — 미니오디오 백엔드와 같은 규약.
	m_buses.push_back(MakeOwnerPtr<CEmptyAudioBus>(AUDIO_MASTER_BUS_NAME));
	for (const AudioBusDef& bus : buses)
	{
		if (IsSameAudioBusName(bus.Name.c_str(), AUDIO_MASTER_BUS_NAME))
		{
			m_buses.front()->SetVolume(ClampAudioVolume(bus.Volume));
			continue;
		}
		if (bus.Name.empty()) continue;
		if (m_buses.size() >= MAX_AUDIO_BUSES) break;
		OwnerPtr<CEmptyAudioBus> created = MakeOwnerPtr<CEmptyAudioBus>(bus.Name.c_str());
		created->SetVolume(ClampAudioVolume(bus.Volume));
		m_buses.push_back(std::move(created));
	}
}

std::vector<std::string> CEmptyAudioDevice::GetBusNames() const
{
	std::vector<std::string> names;
	names.reserve(m_buses.size());
	for (const OwnerPtr<CEmptyAudioBus>& bus : m_buses)
	{
		if (bus) names.emplace_back(bus->GetName());
	}
	return names;
}

OwnerPtr<IAudioPlayer> CEmptyAudioDevice::CreatePlayer(const AudioPlayerDesc&)
{
	return MakeOwnerPtr<CEmptyAudioPlayer>();
}

OwnerPtr<IAudioBus> CEmptyAudioDevice::CreateBus(const char* name)
{
	return MakeOwnerPtr<CEmptyAudioBus>(name);
}

SafePtr<IAudioBus> CEmptyAudioDevice::GetBus(const char* name)
{
	const char* wanted = ResolveAudioBusName(name);
	for (OwnerPtr<CEmptyAudioBus>& bus : m_buses)
	{
		if (bus && IsSameAudioBusName(bus->GetName(), wanted))
		{
			return bus.GetSafePtr();
		}
	}
	// 미니오디오 백엔드와 같은 폴백 — 못 찾으면 Master.
	return m_buses.empty() ? SafePtr<IAudioBus>() : m_buses.front().GetSafePtr();
}

OwnerPtr<IAudioEffect> CEmptyAudioDevice::CreateEffect(EAudioEffectKind kind)
{
	return MakeOwnerPtr<CEmptyAudioEffect>(kind);
}

SafePtr<IAudioListener> CEmptyAudioDevice::GetPrimaryListener()
{
	return m_listener ? m_listener.GetSafePtr() : SafePtr<IAudioListener>();
}
