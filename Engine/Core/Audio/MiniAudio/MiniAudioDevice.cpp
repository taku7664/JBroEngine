#include "pch.h"
#include "MiniAudioDevice.h"

#if defined(JBRO_HAS_MINIAUDIO) && JBRO_HAS_MINIAUDIO

#include "ThirdParty/miniaudio/miniaudio.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CMiniAudioDevice 구현 — 골격
//
//  PR A 단계에서는 인터페이스 시그니처 + miniaudio 통합의 발판만 두고,
//  실제 재생/공간음향/마커 콜백 같은 본격 기능은 단계 B 이후 채워진다.
//  ma_engine 만 초기화/종료하고 GetGlobalAudioTimeSeconds 등 시간 인터페이스를
//  이미 작동시킨다 — 향후 리듬게임용 PR 에서 바로 사용 가능하도록.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// ── Player ─────────────────────────────────────────────────────────────────
// 빈 스텁 — Desc 기반 CreatePlayer 가 아직 자산 PCM 라우팅을 채우지 못한 동안 사용.
class CMiniAudioPlayerStub final : public IAudioPlayer
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

// 파일 경로 기반 실제 Player — 에디터 미리듣기에 사용.
// 소멸 시 ma_sound_uninit 으로 리소스를 반드시 해제한다 (스트림/디코더/콜백 모두).
class CMiniAudioFilePlayer final : public IAudioPlayer
{
public:
	CMiniAudioFilePlayer(ma_engine* engine, const char* filePathUtf8)
		: m_engine(engine)
	{
		if (nullptr == engine || nullptr == filePathUtf8) return;
		// STREAM 플래그를 쓰면 ma_sound_get_length_in_seconds 가 0 을 반환해
		// 인스펙터의 슬라이더가 표시되지 않는다. 프리뷰는 전체 디코딩 비용을 감수.
		// (NO_SPATIALIZATION = 2D 처럼 평면 출력, 위치 무관)
		const ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION;

#if defined(_WIN32)
		// 사용자 폴더에 비-ASCII(예: 한글) 가 있으면 ma_sound_init_from_file 의 char* 경로가
		// 시스템 코드페이지로 해석돼 파일 열기 실패 → 무음. UTF-8 입력을 wide 로 변환해 _w 변종 사용.
		const int wlen = MultiByteToWideChar(CP_UTF8, 0, filePathUtf8, -1, nullptr, 0);
		if (wlen > 0)
		{
			std::wstring wpath(static_cast<std::size_t>(wlen), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, filePathUtf8, -1, wpath.data(), wlen);
			// MultiByteToWideChar 가 null 종단도 포함시켰으므로 resize 로 잘라낸다.
			if (false == wpath.empty() && L'\0' == wpath.back()) wpath.pop_back();
			if (MA_SUCCESS == ma_sound_init_from_file_w(engine, wpath.c_str(), flags, nullptr, nullptr, &m_sound))
			{
				m_initialized = true;
				return;
			}
		}
#endif

		if (MA_SUCCESS == ma_sound_init_from_file(engine, filePathUtf8, flags, nullptr, nullptr, &m_sound))
		{
			m_initialized = true;
		}
	}

	~CMiniAudioFilePlayer() override
	{
		Cleanup();
	}

	void Play() override
	{
		if (false == m_initialized) return;
		if (ma_sound_at_end(&m_sound))
		{
			ma_sound_seek_to_pcm_frame(&m_sound, 0);
		}
		ma_sound_start(&m_sound);
	}

	void Pause() override
	{
		if (m_initialized) ma_sound_stop(&m_sound);
	}

	void Stop() override
	{
		if (false == m_initialized) return;
		ma_sound_stop(&m_sound);
		ma_sound_seek_to_pcm_frame(&m_sound, 0);
	}

	bool IsPlaying() const override
	{
		return m_initialized && MA_TRUE == ma_sound_is_playing(&m_sound);
	}

	bool IsEnded() const override
	{
		return false == m_initialized || MA_TRUE == ma_sound_at_end(&m_sound);
	}

	void PlayAt(double) override { Play(); }

	std::uint64_t GetPositionFrames() const override
	{
		if (false == m_initialized) return 0;
		ma_uint64 cursor = 0;
		ma_sound_get_cursor_in_pcm_frames(const_cast<ma_sound*>(&m_sound), &cursor);
		return cursor;
	}

	double GetPositionSeconds() const override
	{
		if (false == m_initialized) return 0.0;
		float seconds = 0.0f;
		ma_sound_get_cursor_in_seconds(const_cast<ma_sound*>(&m_sound), &seconds);
		return static_cast<double>(seconds);
	}

	double GetDurationSeconds() const override
	{
		if (false == m_initialized) return 0.0;
		float seconds = 0.0f;
		ma_sound_get_length_in_seconds(const_cast<ma_sound*>(&m_sound), &seconds);
		return static_cast<double>(seconds);
	}

	void Seek(std::uint64_t frame) override
	{
		if (m_initialized) ma_sound_seek_to_pcm_frame(&m_sound, frame);
	}

	void SeekSeconds(double seconds) override
	{
		if (false == m_initialized) return;
		ma_uint64 totalFrames = 0;
		if (MA_SUCCESS != ma_sound_get_length_in_pcm_frames(&m_sound, &totalFrames) || 0 == totalFrames)
		{
			IAudioPlayer::SeekSeconds(seconds);
			return;
		}
		float lengthSec = 0.0f;
		if (MA_SUCCESS != ma_sound_get_length_in_seconds(&m_sound, &lengthSec) || lengthSec <= 0.0f)
		{
			IAudioPlayer::SeekSeconds(seconds);
			return;
		}
		const double frac = std::max(0.0, std::min(1.0, seconds / static_cast<double>(lengthSec)));
		const ma_uint64 target = static_cast<ma_uint64>(frac * totalFrames);
		ma_sound_seek_to_pcm_frame(&m_sound, target);
	}

	void SetVolume(float v) override
	{
		if (m_initialized) ma_sound_set_volume(&m_sound, v);
	}

	void SetPitch(float p) override
	{
		if (m_initialized) ma_sound_set_pitch(&m_sound, p);
	}

	void SetLoop(bool loop) override
	{
		if (m_initialized) ma_sound_set_looping(&m_sound, loop ? MA_TRUE : MA_FALSE);
	}

	void SetPosition(AudioVec3) override {}
	void SetSpatial (const AudioSpatialParams&) override {}
	void AttachEffect(SafePtr<IAudioEffect>) override {}
	void DetachAllEffects() override {}

	bool IsInitialized() const { return m_initialized; }

private:
	void Cleanup()
	{
		if (false == m_initialized) return;
		ma_sound_stop(&m_sound);
		ma_sound_uninit(&m_sound);
		m_initialized = false;
	}

	ma_engine* m_engine = nullptr;
	ma_sound   m_sound{};
	bool       m_initialized = false;
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
	return MakeOwnerPtr<CMiniAudioPlayerStub>();
}

OwnerPtr<IAudioPlayer> CMiniAudioDevice::CreatePlayerFromFile(const char* filePathUtf8)
{
	if (!m_impl || false == m_impl->EngineInitialized || nullptr == filePathUtf8)
	{
		return MakeOwnerPtr<CMiniAudioPlayerStub>();
	}
	OwnerPtr<CMiniAudioFilePlayer> player = MakeOwnerPtr<CMiniAudioFilePlayer>(&m_impl->Engine, filePathUtf8);
	if (false == player->IsInitialized())
	{
		// 로딩 실패해도 stub 으로 폴백 — 호출자는 IsPlaying/IsEnded 만으로 안전하게 처리 가능.
		return MakeOwnerPtr<CMiniAudioPlayerStub>();
	}
	return player;
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
