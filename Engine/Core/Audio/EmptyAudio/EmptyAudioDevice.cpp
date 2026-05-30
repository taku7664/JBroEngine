#include "pch.h"
#include "EmptyAudioDevice.h"

bool CEmptyAudioDevice::Initialize(const AudioDeviceDesc&)
{
	m_listener = MakeOwnerPtr<CEmptyAudioListener>();
	return static_cast<bool>(m_listener);
}

void CEmptyAudioDevice::Finalize()
{
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

OwnerPtr<IAudioEffect> CEmptyAudioDevice::CreateEffect(EAudioEffectKind kind)
{
	return MakeOwnerPtr<CEmptyAudioEffect>(kind);
}

SafePtr<IAudioListener> CEmptyAudioDevice::GetPrimaryListener()
{
	return m_listener ? m_listener.GetSafePtr() : SafePtr<IAudioListener>();
}
