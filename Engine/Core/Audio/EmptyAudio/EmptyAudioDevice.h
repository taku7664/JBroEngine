#pragma once

#include "Core/Audio/IAudioDevice.h"
#include "Core/Audio/IAudioPlayer.h"
#include "Core/Audio/IAudioListener.h"
#include "Core/Audio/IAudioBus.h"
#include "Core/Audio/IAudioEffect.h"

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CEmptyAudioDevice ─ no-op 백엔드
//
//  헤드리스 / CI / 사운드 디바이스 없는 환경에서 사용. 모든 메서드는 안전한
//  기본값을 반환하고 부작용 없이 종료된다. RHI 의 CEmptyRHIDevice 와 같은
//  역할.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class CEmptyAudioPlayer final : public IAudioPlayer
{
public:
	void Play()  override {}
	void Pause() override {}
	void Stop()  override {}
	bool IsPlaying() const override { return false; }
	bool IsEnded()   const override { return true;  }

	void PlayAt(double) override {}
	std::uint64_t GetPositionFrames()  const override { return 0; }
	double        GetPositionSeconds() const override { return 0.0; }
	double        GetDurationSeconds() const override { return 0.0; }
	void Seek(std::uint64_t) override {}

	void SetVolume(float) override {}
	void SetPitch (float) override {}
	void SetLoop  (bool ) override {}

	void SetPosition(AudioVec3) override {}
	void SetSpatial (const AudioSpatialParams&) override {}

	void AttachEffect(SafePtr<IAudioEffect>) override {}
	void DetachAllEffects() override {}
};

class CEmptyAudioListener final : public IAudioListener
{
public:
	void SetPosition    (AudioVec3) override {}
	void SetForward     (AudioVec3) override {}
	void SetMasterVolume(float)     override {}
	void SetOcclusionForPlayer(SafePtr<IAudioPlayer>, float) override {}
};

class CEmptyAudioBus final : public IAudioBus
{
public:
	explicit CEmptyAudioBus(EAudioBusKind kind) : m_kind(kind) {}
	EAudioBusKind GetKind() const override { return m_kind; }
	void  SetVolume(float v) override { m_volume = v; }
	float GetVolume() const override { return m_volume; }
	void  SetMuted (bool m)  override { m_muted = m; }
	bool  IsMuted  () const override { return m_muted; }
	void  AttachEffect(SafePtr<IAudioEffect>) override {}
	void  DetachAllEffects() override {}
private:
	EAudioBusKind m_kind  = EAudioBusKind::Master;
	float         m_volume = 1.0f;
	bool          m_muted  = false;
};

class CEmptyAudioEffect final : public IAudioEffect
{
public:
	explicit CEmptyAudioEffect(EAudioEffectKind kind) : m_kind(kind) {}
	EAudioEffectKind GetKind() const override { return m_kind; }
	void  SetParameter(const char*, float) override {}
	float GetParameter(const char*) const override { return 0.0f; }
private:
	EAudioEffectKind m_kind = EAudioEffectKind::Reverb;
};

class CEmptyAudioDevice final : public IAudioDevice
{
public:
	bool Initialize(const AudioDeviceDesc&) override;
	void Finalize() override;
	void Tick(float) override {}

	OwnerPtr<IAudioPlayer>  CreatePlayer (const AudioPlayerDesc&) override;
	OwnerPtr<IAudioBus>     CreateBus    (EAudioBusKind kind) override;
	OwnerPtr<IAudioEffect>  CreateEffect (EAudioEffectKind kind) override;
	SafePtr<IAudioListener> GetPrimaryListener() override;

	double GetGlobalAudioTimeSeconds() const override { return 0.0; }
	double GetOutputLatencySeconds()  const override { return 0.0; }
	void RegisterPlayerMarker(SafePtr<IAudioPlayer>, std::uint64_t, std::function<void()>) override {}

	void  SetMasterVolume(float v) override { m_master = v; }
	float GetMasterVolume() const override { return m_master; }

private:
	OwnerPtr<CEmptyAudioListener> m_listener;
	float                         m_master = 1.0f;
};
