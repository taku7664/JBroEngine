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
	CMiniAudioFilePlayer(ma_engine* engine, const char* filePathUtf8, ma_sound_group* group)
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
			if (MA_SUCCESS == ma_sound_init_from_file_w(engine, wpath.c_str(), flags, group, nullptr, &m_sound))
			{
				m_initialized = true;
				return;
			}
		}
#endif

		if (MA_SUCCESS == ma_sound_init_from_file(engine, filePathUtf8, flags, group, nullptr, &m_sound))
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

	// 효과 노드를 sound 와 출력(endpoint) 사이에 삽입: sound → effect → endpoint.
	// effect 는 SafePtr 로 보관해 재생 중 살아있게 한다.
	void AttachEffect(SafePtr<IAudioEffect> effect) override;
	void DetachAllEffects() override;

	bool IsInitialized() const { return m_initialized; }

private:
	void Cleanup()
	{
		DetachAllEffects();
		if (false == m_initialized) return;
		ma_sound_stop(&m_sound);
		ma_sound_uninit(&m_sound);
		m_initialized = false;
	}

	ma_engine*            m_engine = nullptr;
	ma_sound              m_sound{};
	bool                  m_initialized = false;
	SafePtr<IAudioEffect> m_attachedEffect;
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

// ── Bus ─────────────────────────────────────────────────────────────────────
// ma_sound_group 노드를 소유. parent=null 이면 endpoint 직결(Master), 아니면 parent 로.
// player 는 이 group 으로 라우팅되어 카테고리 볼륨/뮤트가 일괄 적용된다.
class CMiniAudioBus final : public IAudioBus
{
public:
	CMiniAudioBus(ma_engine* engine, EAudioBusKind kind, ma_sound_group* parent)
		: m_kind(kind)
	{
		if (nullptr == engine) return;
		if (MA_SUCCESS == ma_sound_group_init(engine, 0, parent, &m_group))
		{
			m_initialized = true;
		}
	}

	~CMiniAudioBus() override
	{
		if (m_initialized) ma_sound_group_uninit(&m_group);
	}

	EAudioBusKind GetKind() const override { return m_kind; }

	void SetVolume(float v) override
	{
		m_volume = v;
		if (m_initialized) ma_sound_group_set_volume(&m_group, m_muted ? 0.0f : v);
	}
	float GetVolume() const override { return m_volume; }

	void SetMuted(bool m) override
	{
		m_muted = m;
		if (m_initialized) ma_sound_group_set_volume(&m_group, m ? 0.0f : m_volume);
	}
	bool IsMuted() const override { return m_muted; }

	void AttachEffect(SafePtr<IAudioEffect>) override {}   // G-4
	void DetachAllEffects() override {}                    // G-4

	// mini 전용 — player 라우팅용 노드 핸들. null 이면 미초기화.
	ma_sound_group* GetGroup() { return m_initialized ? &m_group : nullptr; }

private:
	EAudioBusKind   m_kind        = EAudioBusKind::Master;
	ma_sound_group  m_group{};
	bool            m_initialized = false;
	float           m_volume      = 1.0f;
	bool            m_muted       = false;
};

// ── Freeverb 리버브 노드 ─────────────────────────────────────────────────────
// Jezar 의 Freeverb (Schroeder-Moorer): 채널당 comb 8 + allpass 4.
// miniaudio 에 내장 reverb 가 없어 커스텀 ma_node 로 구현. 1 in / 1 out, 채널 보존.
// 파라미터: roomSize / damping / wet / dry / width (0..1).
namespace
{
	// 48kHz 기준 Freeverb 표준 tuning (샘플 수). 다른 sample rate 도 근사로 충분.
	constexpr int FV_COMB_TUNING[8]    = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
	constexpr int FV_ALLPASS_TUNING[4] = { 556, 441, 341, 225 };
	constexpr int FV_STEREO_SPREAD     = 23;
	constexpr float FV_FIXED_GAIN      = 0.015f;
	constexpr float FV_SCALE_ROOM      = 0.28f;
	constexpr float FV_OFFSET_ROOM     = 0.7f;
	constexpr float FV_SCALE_DAMP      = 0.4f;

	struct CombFilter
	{
		std::vector<float> Buffer;
		int   Index = 0;
		float Feedback = 0.0f;
		float Damp1 = 0.0f, Damp2 = 0.0f;
		float Store = 0.0f;

		void SetSize(int n) { Buffer.assign(n > 0 ? n : 1, 0.0f); Index = 0; Store = 0.0f; }
		void SetDamp(float d) { Damp1 = d; Damp2 = 1.0f - d; }
		inline float Process(float input)
		{
			float output = Buffer[Index];
			Store = output * Damp2 + Store * Damp1;
			Buffer[Index] = input + Store * Feedback;
			if (++Index >= static_cast<int>(Buffer.size())) Index = 0;
			return output;
		}
	};

	struct AllpassFilter
	{
		std::vector<float> Buffer;
		int Index = 0;

		void SetSize(int n) { Buffer.assign(n > 0 ? n : 1, 0.0f); Index = 0; }
		inline float Process(float input)
		{
			float bufout = Buffer[Index];
			float output = -input + bufout;
			Buffer[Index] = input + bufout * 0.5f;   // allpass feedback 고정 0.5
			if (++Index >= static_cast<int>(Buffer.size())) Index = 0;
			return output;
		}
	};

	// ma_node — 반드시 ma_node_base 가 첫 멤버.
	struct FreeverbNode
	{
		ma_node_base Base{};
		ma_uint32    Channels = 2;

		// 모노 처리 코어 1세트(스테레오면 spread offset 적용한 2세트).
		CombFilter    CombL[8];
		AllpassFilter AllpassL[4];
		CombFilter    CombR[8];
		AllpassFilter AllpassR[4];

		float RoomSize = 0.5f, Damping = 0.5f, Wet = 0.3f, Dry = 0.7f, Width = 1.0f;

		void Rebuild(ma_uint32 sampleRate)
		{
			const float srScale = (sampleRate > 0) ? static_cast<float>(sampleRate) / 44100.0f : 1.0f;
			for (int i = 0; i < 8; ++i)
			{
				CombL[i].SetSize(static_cast<int>(FV_COMB_TUNING[i] * srScale));
				CombR[i].SetSize(static_cast<int>((FV_COMB_TUNING[i] + FV_STEREO_SPREAD) * srScale));
			}
			for (int i = 0; i < 4; ++i)
			{
				AllpassL[i].SetSize(static_cast<int>(FV_ALLPASS_TUNING[i] * srScale));
				AllpassR[i].SetSize(static_cast<int>((FV_ALLPASS_TUNING[i] + FV_STEREO_SPREAD) * srScale));
			}
			UpdateParams();
		}

		void UpdateParams()
		{
			const float fb   = RoomSize * FV_SCALE_ROOM + FV_OFFSET_ROOM;
			const float damp = Damping * FV_SCALE_DAMP;
			for (int i = 0; i < 8; ++i)
			{
				CombL[i].Feedback = fb; CombL[i].SetDamp(damp);
				CombR[i].Feedback = fb; CombR[i].SetDamp(damp);
			}
		}
	};

	void FreeverbProcess(ma_node* pNode, const float** ppFramesIn, ma_uint32* pFrameCountIn,
	                     float** ppFramesOut, ma_uint32* pFrameCountOut)
	{
		FreeverbNode* fv = reinterpret_cast<FreeverbNode*>(pNode);
		const float* in  = ppFramesIn[0];
		float*       out = ppFramesOut[0];
		const ma_uint32 frames = *pFrameCountOut;
		const ma_uint32 ch = fv->Channels;

		const float wet1 = fv->Wet * (fv->Width * 0.5f + 0.5f);
		const float wet2 = fv->Wet * ((1.0f - fv->Width) * 0.5f);

		for (ma_uint32 f = 0; f < frames; ++f)
		{
			const float inL = in[f * ch + 0];
			const float inR = (ch > 1) ? in[f * ch + 1] : inL;
			const float input = (inL + inR) * FV_FIXED_GAIN;

			float outL = 0.0f, outR = 0.0f;
			for (int i = 0; i < 8; ++i) { outL += fv->CombL[i].Process(input); outR += fv->CombR[i].Process(input); }
			for (int i = 0; i < 4; ++i) { outL = fv->AllpassL[i].Process(outL); outR = fv->AllpassR[i].Process(outR); }

			out[f * ch + 0] = inL * fv->Dry + outL * wet1 + outR * wet2;
			if (ch > 1) out[f * ch + 1] = inR * fv->Dry + outR * wet1 + outL * wet2;
		}
		(void)pFrameCountIn;
	}

	ma_node_vtable g_freeverbVtable = { FreeverbProcess, nullptr, 1, 1, 0 };
}

// ── DSP 효과 ─────────────────────────────────────────────────────────────────
// Kind 에 따라 miniaudio 내장 필터 노드(lpf/hpf/delay) 또는 커스텀 Freeverb 노드를
// 소유한다. 파라미터는 문자열 키(map<string,float> 와 일치) — SetParameter 로 갱신.
// 노드 자체는 엔진 노드 그래프에 init 되며, 체인 배선(AttachEffect)은 후속 단계.
class CMiniAudioEffect final : public IAudioEffect
{
public:
	CMiniAudioEffect(ma_engine* engine, EAudioEffectKind kind)
		: m_kind(kind), m_engine(engine)
	{
		if (nullptr == engine) return;
		ma_node_graph* graph = ma_engine_get_node_graph(engine);
		const ma_uint32 channels   = ma_engine_get_channels(engine);
		const ma_uint32 sampleRate = ma_engine_get_sample_rate(engine);

		switch (kind)
		{
		case EAudioEffectKind::LowPass:
		{
			ma_lpf_node_config cfg = ma_lpf_node_config_init(channels, sampleRate, 1000.0, 2);
			if (MA_SUCCESS == ma_lpf_node_init(graph, &cfg, nullptr, &m_lpf)) { m_node = &m_lpf.baseNode; m_ready = true; }
			break;
		}
		case EAudioEffectKind::HighPass:
		{
			ma_hpf_node_config cfg = ma_hpf_node_config_init(channels, sampleRate, 200.0, 2);
			if (MA_SUCCESS == ma_hpf_node_init(graph, &cfg, nullptr, &m_hpf)) { m_node = &m_hpf.baseNode; m_ready = true; }
			break;
		}
		case EAudioEffectKind::Echo:
		{
			ma_delay_node_config cfg = ma_delay_node_config_init(channels, sampleRate,
				static_cast<ma_uint32>(sampleRate * 0.25f), 0.5f);
			if (MA_SUCCESS == ma_delay_node_init(graph, &cfg, nullptr, &m_delay)) { m_node = &m_delay.baseNode; m_ready = true; }
			break;
		}
		default:   // Reverb / 그 외 → Freeverb
		{
			m_freeverb.Channels = channels;
			m_freeverb.Rebuild(sampleRate);
			ma_node_config cfg = ma_node_config_init();
			cfg.vtable          = &g_freeverbVtable;
			cfg.pInputChannels  = &m_freeverb.Channels;
			cfg.pOutputChannels = &m_freeverb.Channels;
			if (MA_SUCCESS == ma_node_init(graph, &cfg, nullptr, &m_freeverb.Base)) { m_node = &m_freeverb.Base; m_ready = true; m_kind = EAudioEffectKind::Reverb; }
			break;
		}
		}
	}

	~CMiniAudioEffect() override
	{
		if (false == m_ready) return;
		switch (m_kind)
		{
		case EAudioEffectKind::LowPass:  ma_lpf_node_uninit(&m_lpf, nullptr);   break;
		case EAudioEffectKind::HighPass: ma_hpf_node_uninit(&m_hpf, nullptr);   break;
		case EAudioEffectKind::Echo:     ma_delay_node_uninit(&m_delay, nullptr); break;
		default:                         ma_node_uninit(&m_freeverb.Base, nullptr); break;
		}
	}

	EAudioEffectKind GetKind() const override { return m_kind; }

	void SetParameter(const char* name, float value) override
	{
		if (false == m_ready || nullptr == name) return;
		const std::string key = name;

		switch (m_kind)
		{
		case EAudioEffectKind::LowPass:
		{
			if (key == "cutoff")
			{
				ma_lpf_config c = ma_lpf_config_init(ma_format_f32, ma_engine_get_channels(m_engine),
					ma_engine_get_sample_rate(m_engine), value, 2);
				ma_lpf_node_reinit(&c, &m_lpf);
			}
			break;
		}
		case EAudioEffectKind::HighPass:
		{
			if (key == "cutoff")
			{
				ma_hpf_config c = ma_hpf_config_init(ma_format_f32, ma_engine_get_channels(m_engine),
					ma_engine_get_sample_rate(m_engine), value, 2);
				ma_hpf_node_reinit(&c, &m_hpf);
			}
			break;
		}
		case EAudioEffectKind::Echo:
		{
			if (key == "decay") ma_delay_node_set_decay(&m_delay, value);
			else if (key == "wet") ma_delay_node_set_wet(&m_delay, value);
			break;
		}
		default:   // Reverb (Freeverb)
		{
			if      (key == "roomSize") m_freeverb.RoomSize = value;
			else if (key == "damping")  m_freeverb.Damping  = value;
			else if (key == "wet")      m_freeverb.Wet      = value;
			else if (key == "dry")      m_freeverb.Dry      = value;
			else if (key == "width")    m_freeverb.Width    = value;
			m_freeverb.UpdateParams();
			break;
		}
		}
	}

	float GetParameter(const char* name) const override
	{
		if (nullptr == name) return 0.0f;
		const std::string key = name;
		if (EAudioEffectKind::Reverb == m_kind)
		{
			if (key == "roomSize") return m_freeverb.RoomSize;
			if (key == "damping")  return m_freeverb.Damping;
			if (key == "wet")      return m_freeverb.Wet;
			if (key == "dry")      return m_freeverb.Dry;
			if (key == "width")    return m_freeverb.Width;
		}
		return 0.0f;
	}

	// mini 전용 — 체인 배선용 노드 핸들. null 이면 미초기화.
	ma_node* GetNode() { return m_ready ? m_node : nullptr; }

private:
	EAudioEffectKind m_kind   = EAudioEffectKind::Reverb;
	ma_engine*       m_engine = nullptr;
	ma_node*         m_node   = nullptr;
	bool             m_ready  = false;

	ma_lpf_node      m_lpf{};
	ma_hpf_node      m_hpf{};
	ma_delay_node    m_delay{};
	FreeverbNode     m_freeverb{};
};

// ── CMiniAudioFilePlayer 효과 배선 (CMiniAudioEffect 정의 이후) ──────────────
void CMiniAudioFilePlayer::AttachEffect(SafePtr<IAudioEffect> effect)
{
	if (false == m_initialized || nullptr == m_engine) return;
	CMiniAudioEffect* mini = dynamic_cast<CMiniAudioEffect*>(effect.TryGet());
	ma_node* effectNode = mini ? mini->GetNode() : nullptr;
	if (nullptr == effectNode) return;

	// sound → effect → endpoint. (현재 단일 효과만 지원 — 기존 효과는 교체.)
	DetachAllEffects();
	ma_node_attach_output_bus(effectNode, 0, ma_engine_get_endpoint(m_engine), 0);
	ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&m_sound), 0, effectNode, 0);
	m_attachedEffect = effect;
}

void CMiniAudioFilePlayer::DetachAllEffects()
{
	if (false == m_initialized || nullptr == m_engine) return;
	// sound 를 다시 endpoint 로 직결 — 효과 우회.
	ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&m_sound), 0, ma_engine_get_endpoint(m_engine), 0);
	m_attachedEffect.Reset();
}

// ── Impl ──────────────────────────────────────────────────────────────────
struct MiniAudioDeviceImpl
{
	ma_engine                       Engine{};
	bool                            EngineInitialized = false;
	OwnerPtr<CMiniAudioListener>    Listener;
	// 표준 믹싱 버스 — Master(endpoint 직결) ← Music/SFX/Voice/UI/Custom.
	OwnerPtr<CMiniAudioBus>         Buses[static_cast<std::size_t>(EAudioBusKind::Count)];
	float                           MasterVolume = 1.0f;

	CMiniAudioBus* GetBusPtr(EAudioBusKind kind)
	{
		const std::size_t i = static_cast<std::size_t>(kind);
		return (i < static_cast<std::size_t>(EAudioBusKind::Count)) ? Buses[i].Get() : nullptr;
	}
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

	// 표준 믹싱 버스 계층 구성 — Master 는 endpoint 직결(parent=null),
	// 나머지는 Master 의 group 을 parent 로 둬 카테고리 볼륨이 Master 로 합쳐진다.
	{
		const std::size_t masterIdx = static_cast<std::size_t>(EAudioBusKind::Master);
		m_impl->Buses[masterIdx] = MakeOwnerPtr<CMiniAudioBus>(&m_impl->Engine, EAudioBusKind::Master, nullptr);
		ma_sound_group* masterGroup = m_impl->Buses[masterIdx] ? m_impl->Buses[masterIdx]->GetGroup() : nullptr;

		for (std::size_t i = 0; i < static_cast<std::size_t>(EAudioBusKind::Count); ++i)
		{
			if (i == masterIdx) continue;
			m_impl->Buses[i] = MakeOwnerPtr<CMiniAudioBus>(&m_impl->Engine, static_cast<EAudioBusKind>(i), masterGroup);
		}
	}
	return true;
}

void CMiniAudioDevice::Finalize()
{
	if (m_impl)
	{
		// 버스(group)를 엔진보다 먼저 해제 — group 은 엔진 노드 그래프에 속하므로
		// 엔진 uninit 전에 정리해야 안전.
		for (auto& bus : m_impl->Buses) bus.Reset();

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

OwnerPtr<IAudioPlayer> CMiniAudioDevice::CreatePlayerFromFile(const char* filePathUtf8, EAudioBusKind bus)
{
	if (!m_impl || false == m_impl->EngineInitialized || nullptr == filePathUtf8)
	{
		return MakeOwnerPtr<CMiniAudioPlayerStub>();
	}
	// 지정 버스로 라우팅. 버스 미초기화 시 group=null → endpoint 직결(기존 동작).
	CMiniAudioBus* busPtr = m_impl->GetBusPtr(bus);
	ma_sound_group* group = busPtr ? busPtr->GetGroup() : nullptr;

	OwnerPtr<CMiniAudioFilePlayer> player = MakeOwnerPtr<CMiniAudioFilePlayer>(&m_impl->Engine, filePathUtf8, group);
	if (false == player->IsInitialized())
	{
		// 로딩 실패해도 stub 으로 폴백 — 호출자는 IsPlaying/IsEnded 만으로 안전하게 처리 가능.
		return MakeOwnerPtr<CMiniAudioPlayerStub>();
	}
	return player;
}

OwnerPtr<IAudioBus> CMiniAudioDevice::CreateBus(EAudioBusKind kind)
{
	// Custom 등 표준 외 버스 — Master 하위에 새 group 생성.
	ma_engine* engine = (m_impl && m_impl->EngineInitialized) ? &m_impl->Engine : nullptr;
	CMiniAudioBus* master = m_impl ? m_impl->GetBusPtr(EAudioBusKind::Master) : nullptr;
	ma_sound_group* parent = master ? master->GetGroup() : nullptr;
	return MakeOwnerPtr<CMiniAudioBus>(engine, kind, parent);
}

SafePtr<IAudioBus> CMiniAudioDevice::GetBus(EAudioBusKind kind)
{
	if (!m_impl) return SafePtr<IAudioBus>();
	const std::size_t i = static_cast<std::size_t>(kind);
	if (i >= static_cast<std::size_t>(EAudioBusKind::Count)) return SafePtr<IAudioBus>();
	return m_impl->Buses[i] ? m_impl->Buses[i].GetSafePtr() : SafePtr<IAudioBus>();
}

OwnerPtr<IAudioEffect> CMiniAudioDevice::CreateEffect(EAudioEffectKind kind)
{
	ma_engine* engine = (m_impl && m_impl->EngineInitialized) ? &m_impl->Engine : nullptr;
	return MakeOwnerPtr<CMiniAudioEffect>(engine, kind);
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
