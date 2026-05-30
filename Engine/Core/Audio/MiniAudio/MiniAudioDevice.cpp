#include "pch.h"
#include "MiniAudioDevice.h"

#if defined(JBRO_HAS_MINIAUDIO) && JBRO_HAS_MINIAUDIO

#include "ThirdParty/miniaudio/miniaudio.h"

#include <unordered_map>
#include <vector>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CMiniAudioDevice 구현 — 골격
//
//  PR A 단계에서는 인터페이스 시그니처 + miniaudio 통합의 발판만 두고,
//  실제 재생/공간음향/마커 콜백 같은 본격 기능은 단계 B 이후 채워진다.
//  ma_engine 만 초기화/종료하고 GetGlobalAudioTimeSeconds 등 시간 인터페이스를
//  이미 작동시킨다 — 향후 리듬게임용 PR 에서 바로 사용 가능하도록.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// ── Player ─────────────────────────────────────────────────────────────────
class CMiniAudioPlayer final : public IAudioPlayer
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
	void SetLoop  (bool)  override {}
	void SetPosition(AudioVec3) override {}
	void SetSpatial (const AudioSpatialParams&) override {}
	void AttachEffect(SafePtr<IAudioEffect>) override {}
	void DetachAllEffects() override {}
};

// ── Listener ────────────────────────────────────────────────────────────────
class CMiniAudioListener final : public IAudioListener
{
public:
	void SetPosition    (AudioVec3) override {}
	void SetForward     (AudioVec3) override {}
	void SetMasterVolume(float)     override {}
	void SetOcclusionForPlayer(SafePtr<IAudioPlayer>, float) override {}
};

// ── Bus / Effect (단계 G 에서 본격 구현) ───────────────────────────────────
class CMiniAudioBus final : public IAudioBus
{
public:
	explicit CMiniAudioBus(EAudioBusKind kind) : m_kind(kind) {}
	EAudioBusKind GetKind() const override { return m_kind; }
	void  SetVolume(float v) override { m_volume = v; }
	float GetVolume() const override { return m_volume; }
	void  SetMuted (bool m)  override { m_muted = m; }
	bool  IsMuted  () const override { return m_muted; }
	void  AttachEffect(SafePtr<IAudioEffect>) override {}
	void  DetachAllEffects() override {}
private:
	EAudioBusKind m_kind   = EAudioBusKind::Master;
	float         m_volume = 1.0f;
	bool          m_muted  = false;
};

class CMiniAudioEffect final : public IAudioEffect
{
public:
	explicit CMiniAudioEffect(EAudioEffectKind kind) : m_kind(kind) {}
	EAudioEffectKind GetKind() const override { return m_kind; }
	void  SetParameter(const char*, float) override {}
	float GetParameter(const char*) const override { return 0.0f; }
private:
	EAudioEffectKind m_kind = EAudioEffectKind::Reverb;
};

// ── Impl ──────────────────────────────────────────────────────────────────
struct MiniAudioDeviceImpl
{
	ma_engine                       Engine{};
	bool                            EngineInitialized = false;
	OwnerPtr<CMiniAudioListener>    Listener;
	float                           MasterVolume = 1.0f;
};

CMiniAudioDevice::CMiniAudioDevice()  = default;
CMiniAudioDevice::~CMiniAudioDevice() = default;

bool CMiniAudioDevice::Initialize(const AudioDeviceDesc& desc)
{
	m_impl = MakeOwnerPtr<MiniAudioDeviceImpl>();
	if (!m_impl) return false;

	ma_engine_config cfg = ma_engine_config_init();
	cfg.sampleRate = desc.Format.SampleRate;
	cfg.channels   = desc.Format.Channels;

	if (MA_SUCCESS != ma_engine_init(&cfg, &m_impl->Engine))
	{
		m_impl.Reset();
		return false;
	}
	m_impl->EngineInitialized = true;
	m_impl->Listener = MakeOwnerPtr<CMiniAudioListener>();
	return true;
}

void CMiniAudioDevice::Finalize()
{
	if (m_impl)
	{
		if (m_impl->EngineInitialized)
		{
			ma_engine_uninit(&m_impl->Engine);
			m_impl->EngineInitialized = false;
		}
		m_impl.Reset();
	}
}

void CMiniAudioDevice::Tick(float)
{
	// PR A 단계에서는 별다른 일 없음 — 단계 B 이후 ended player GC, marker
	// dispatch 등을 여기 채움.
}

OwnerPtr<IAudioPlayer> CMiniAudioDevice::CreatePlayer(const AudioPlayerDesc&)
{
	// PR A 단계: 빈 player 만 반환. 실제 ma_sound 생성은 자산 PR(B) 에서.
	return MakeOwnerPtr<CMiniAudioPlayer>();
}

OwnerPtr<IAudioBus> CMiniAudioDevice::CreateBus(EAudioBusKind kind)
{
	return MakeOwnerPtr<CMiniAudioBus>(kind);
}

OwnerPtr<IAudioEffect> CMiniAudioDevice::CreateEffect(EAudioEffectKind kind)
{
	return MakeOwnerPtr<CMiniAudioEffect>(kind);
}

SafePtr<IAudioListener> CMiniAudioDevice::GetPrimaryListener()
{
	return (m_impl && m_impl->Listener) ? m_impl->Listener.GetSafePtr() : SafePtr<IAudioListener>();
}

double CMiniAudioDevice::GetGlobalAudioTimeSeconds() const
{
	if (!m_impl || !m_impl->EngineInitialized) return 0.0;
	return static_cast<double>(ma_engine_get_time_in_milliseconds(&m_impl->Engine)) / 1000.0;
}

double CMiniAudioDevice::GetOutputLatencySeconds() const
{
	if (!m_impl || !m_impl->EngineInitialized) return 0.0;
	ma_device* device = ma_engine_get_device(&m_impl->Engine);
	if (nullptr == device) return 0.0;
	const ma_uint32 frames = device->playback.internalPeriodSizeInFrames * device->playback.internalPeriods;
	const ma_uint32 sr     = device->sampleRate;
	return sr > 0 ? static_cast<double>(frames) / static_cast<double>(sr) : 0.0;
}

void CMiniAudioDevice::RegisterPlayerMarker(SafePtr<IAudioPlayer>, std::uint64_t, std::function<void()>)
{
	// 단계 G 또는 리듬게임 PR 에서 본격 구현. 현재는 no-op.
}

void  CMiniAudioDevice::SetMasterVolume(float v)
{
	if (!m_impl) return;
	m_impl->MasterVolume = v;
	if (m_impl->EngineInitialized)
	{
		ma_engine_set_volume(&m_impl->Engine, v);
	}
}

float CMiniAudioDevice::GetMasterVolume() const
{
	return m_impl ? m_impl->MasterVolume : 1.0f;
}

#endif // JBRO_HAS_MINIAUDIO
