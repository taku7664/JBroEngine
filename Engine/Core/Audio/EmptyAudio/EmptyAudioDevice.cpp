#include "pch.h"
#include "EmptyAudioDevice.h"

bool CEmptyAudioDevice::Initialize(const AudioDeviceDesc&)
{
	m_listener = MakeOwnerPtr<CEmptyAudioListener>();

	// 표준 믹싱 버스 — no-op 백엔드라 상태만 보관. GetBus 진입점 일관성 유지.
	for (std::size_t i = 0; i < static_cast<std::size_t>(EAudioBusKind::Count); ++i)
	{
		m_buses[i] = MakeOwnerPtr<CEmptyAudioBus>(static_cast<EAudioBusKind>(i));
	}
	return static_cast<bool>(m_listener);
}

void CEmptyAudioDevice::Finalize()
{
	for (auto& bus : m_buses) bus.Reset();
	m_listener.Reset();
}

OwnerPtr<IAudioPlayer> CEmptyAudioDevice::CreatePlayer(const AudioPlayerDesc&)
{
	return MakeOwnerPtr<CEmptyAudioPlayer>();
}

OwnerPtr<IAudioBus> CEmptyAudioDevice::CreateBus(EAudioBusKind kind)
{
	return MakeOwnerPtr<CEmptyAudioBus>(kind);
}

SafePtr<IAudioBus> CEmptyAudioDevice::GetBus(EAudioBusKind kind)
{
	const std::size_t i = static_cast<std::size_t>(kind);
	if (i >= static_cast<std::size_t>(EAudioBusKind::Count)) return SafePtr<IAudioBus>();
	return m_buses[i] ? m_buses[i].GetSafePtr() : SafePtr<IAudioBus>();
}

OwnerPtr<IAudioEffect> CEmptyAudioDevice::CreateEffect(EAudioEffectKind kind)
{
	return MakeOwnerPtr<CEmptyAudioEffect>(kind);
}

SafePtr<IAudioListener> CEmptyAudioDevice::GetPrimaryListener()
{
	return m_listener ? m_listener.GetSafePtr() : SafePtr<IAudioListener>();
}
