#include "pch.h"
#include "MiniAudioDevice.h"

#if defined(JBRO_HAS_MINIAUDIO) && JBRO_HAS_MINIAUDIO

#include "ThirdParty/miniaudio/miniaudio.h"

#include <algorithm>
#include <cstdio>
#include <memory>
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

enum class EMiniAudioBackendChildKind : std::uint8_t
{
	Player,
	Effect,
	CustomBus,
	StandardBus,
	MasterBus,
};

class IMiniAudioBackendChild
{
public:
	virtual ~IMiniAudioBackendChild() = default;

	virtual EMiniAudioBackendChildKind GetBackendChildKind() const = 0;
	virtual void OnBackendChildInvalidating(IMiniAudioBackendChild*) {}
	virtual void ShutdownBackendResource() = 0;
};

// Device와 자식 객체가 함께 참조하는 backend 상태. Device::Finalize()는 등록된
// 자식을 의존 순서대로 무효화하고 engine을 내린다. 외부 OwnerPtr가 더 오래 살아도
// 자식 소멸자는 이미 종료된 ma_engine을 다시 건드리지 않는다.
struct MiniAudioBackendState
{
	ma_engine Engine{};
	bool EngineInitialized = false;
	bool ShuttingDown = false;
	std::vector<IMiniAudioBackendChild*> Children;

	bool IsOperational() const
	{
		return EngineInitialized && false == ShuttingDown;
	}

	void RegisterChild(IMiniAudioBackendChild* child)
	{
		if (nullptr != child)
		{
			Children.push_back(child);
		}
	}

	void ShutdownChild(IMiniAudioBackendChild* child)
	{
		auto it = std::find(Children.begin(), Children.end(), child);
		if (Children.end() == it)
		{
			return;
		}

		Children.erase(it);
		const std::vector<IMiniAudioBackendChild*> peers = Children;
		for (IMiniAudioBackendChild* peer : peers)
		{
			if (nullptr != peer)
			{
				peer->OnBackendChildInvalidating(child);
			}
		}
		child->ShutdownBackendResource();
	}

	std::size_t CountChildren(EMiniAudioBackendChildKind kind) const
	{
		return static_cast<std::size_t>(std::count_if(
			Children.begin(), Children.end(),
			[kind](const IMiniAudioBackendChild* child) {
				return nullptr != child && child->GetBackendChildKind() == kind;
			}));
	}

	void ShutdownChildren(EMiniAudioBackendChildKind kind)
	{
		while (true)
		{
			auto it = std::find_if(
				Children.begin(), Children.end(),
				[kind](const IMiniAudioBackendChild* child) {
					return nullptr != child && child->GetBackendChildKind() == kind;
				});
			if (Children.end() == it)
			{
				return;
			}
			ShutdownChild(*it);
		}
	}

	void Finalize()
	{
		if (false == EngineInitialized)
		{
			return;
		}

		ShuttingDown = true;
		ma_engine_stop(&Engine);

// CRT 의 _DEBUG 가 아니라 엔진 자체 매크로를 쓴다. 호스트와 스크립트 DLL 의 CRT 를 통일하려고
// 모든 구성이 릴리즈 CRT(/MD)를 쓰는데, _DEBUG 를 정의하면 CRT 헤더가 _calloc_dbg /
// _CrtDbgReport 같은 디버그 전용 심볼을 요구해 링크가 깨진다.
#if defined(JBRO_DEBUG)
		const std::size_t players = CountChildren(EMiniAudioBackendChildKind::Player);
		const std::size_t effects = CountChildren(EMiniAudioBackendChildKind::Effect);
		const std::size_t customBuses = CountChildren(EMiniAudioBackendChildKind::CustomBus);
		if (0 != players || 0 != effects || 0 != customBuses)
		{
			std::fprintf(
				stderr,
				"[Audio] Device finalized with live backend children: players=%zu, effects=%zu, customBuses=%zu.\n",
				players, effects, customBuses);
		}
#endif

		ShutdownChildren(EMiniAudioBackendChildKind::Player);
		ShutdownChildren(EMiniAudioBackendChildKind::Effect);
		ShutdownChildren(EMiniAudioBackendChildKind::CustomBus);
		ShutdownChildren(EMiniAudioBackendChildKind::StandardBus);
		ShutdownChildren(EMiniAudioBackendChildKind::MasterBus);

		ma_engine_uninit(&Engine);
		EngineInitialized = false;
	}
};

// 엔진 열거형 → miniaudio 열거형. 새 값을 엔진 쪽에 추가하면 여기도 함께 채울 것.
ma_attenuation_model ToMiniAudioAttenuation(EAudioAttenuationModel model)
{
	switch (model)
	{
	case EAudioAttenuationModel::None:        return ma_attenuation_model_none;
	case EAudioAttenuationModel::Linear:      return ma_attenuation_model_linear;
	case EAudioAttenuationModel::Exponential: return ma_attenuation_model_exponential;
	case EAudioAttenuationModel::Inverse:     return ma_attenuation_model_inverse;
	}
	return ma_attenuation_model_inverse;
}

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
	void SetEffectChain(const std::vector<SafePtr<IAudioEffect>>&) override {}
	void DetachAllEffects() override {}
};

// 파일 경로 기반 실제 Player — 에디터 미리듣기에 사용.
// 소멸 시 ma_sound_uninit 으로 리소스를 반드시 해제한다 (스트림/디코더/콜백 모두).
class CMiniAudioFilePlayer final : public IAudioPlayer, public IMiniAudioBackendChild
{
public:
	CMiniAudioFilePlayer(
		std::shared_ptr<MiniAudioBackendState> state,
		const char* filePathUtf8,
		ma_sound_group* group,
		IMiniAudioBackendChild* busChild)
		: m_state(std::move(state))
		, m_group(group)
		, m_busChild(busChild)
	{
		ma_engine* engine = m_state && m_state->IsOperational() ? &m_state->Engine : nullptr;
		if (nullptr == engine || nullptr == filePathUtf8) return;
		// STREAM 플래그를 쓰면 ma_sound_get_length_in_seconds 가 0 을 반환해
		// 인스펙터의 슬라이더가 표시되지 않는다. 프리뷰는 전체 디코딩 비용을 감수.
		//
		// NO_SPATIALIZATION 은 쓰지 않는다. 그 플래그는 spatializer 노드를 아예 만들지 않아서
		// 나중에 SetSpatial 로 3D 를 켤 수 없다. 대신 노드는 만들어 두고 초기화 직후 꺼서
		// 기본 상태를 2D 로 맞춘다(SetSpatial 을 받지 않는 에디터 미리듣기가 그대로 동작한다).
		const ma_uint32 flags = MA_SOUND_FLAG_DECODE;

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
				OnSoundInitialized();
				return;
			}
		}
#endif

		if (MA_SUCCESS == ma_sound_init_from_file(engine, filePathUtf8, flags, group, nullptr, &m_sound))
		{
			OnSoundInitialized();
		}
	}

	~CMiniAudioFilePlayer() override
	{
		if (m_state)
		{
			m_state->ShutdownChild(this);
		}
		ShutdownBackendResource();
	}

	EMiniAudioBackendChildKind GetBackendChildKind() const override
	{
		return EMiniAudioBackendChildKind::Player;
	}
	void OnBackendChildInvalidating(IMiniAudioBackendChild* child) override;
	void ShutdownBackendResource() override
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

	void SetPosition(AudioVec3 worldPos) override
	{
		if (m_initialized) ma_sound_set_position(&m_sound, worldPos.X, worldPos.Y, worldPos.Z);
	}

	void SetSpatial(const AudioSpatialParams& params) override
	{
		if (false == m_initialized) return;
		// 켜고 끄기는 재생 중에도 안전하다 — spatializer 노드는 생성 시 이미 만들어 두었다.
		ma_sound_set_spatialization_enabled(&m_sound, params.Is3D ? MA_TRUE : MA_FALSE);
		if (false == params.Is3D) return;

		ma_sound_set_attenuation_model(&m_sound, ToMiniAudioAttenuation(params.AttenuationModel));
		// Min > Max 면 miniaudio 가 이상하게 동작하므로 여기서 정리한다.
		const float minDistance = params.MinDistance > 0.0f ? params.MinDistance : 0.0f;
		const float maxDistance = params.MaxDistance > minDistance ? params.MaxDistance : minDistance;
		ma_sound_set_min_distance(&m_sound, minDistance);
		ma_sound_set_max_distance(&m_sound, maxDistance);
		ma_sound_set_rolloff(&m_sound, params.Rolloff > 0.0f ? params.Rolloff : 0.0f);
	}

	// 효과 노드를 sound 와 출력 bus 사이에 삽입한다. SafePtr는 소유하지 않으므로
	// effect가 먼저 파괴되면 backend-state 통지로 이 Player를 즉시 재배선한다.
	void AttachEffect(SafePtr<IAudioEffect> effect) override;
	// 효과 체인 전체를 리스트 순서대로 배선: sound → fx0 → … → fxN → output bus.
	void SetEffectChain(const std::vector<SafePtr<IAudioEffect>>& effects) override;
	void DetachAllEffects() override;

	bool IsInitialized() const { return m_initialized; }

private:
	void Cleanup()
	{
		m_attachedEffect.Reset();
		m_attachedEffectChild = nullptr;
		m_effectChain.clear();
		m_effectChildren.clear();
		if (m_initialized && m_state && m_state->EngineInitialized)
		{
			ma_sound_stop(&m_sound);
			ma_sound_uninit(&m_sound);
		}
		m_initialized = false;
		m_group = nullptr;
		m_busChild = nullptr;
	}

	ma_node* GetOutputNode() const
	{
		if (nullptr != m_group)
		{
			return reinterpret_cast<ma_node*>(m_group);
		}
		return m_state && m_state->EngineInitialized ? ma_engine_get_endpoint(&m_state->Engine) : nullptr;
	}
	void RebuildEffectRouting();

	// 두 초기화 경로(wide / narrow)가 공유하는 마무리.
	// 공간화는 꺼진 상태에서 시작한다 — SetSpatial 을 받지 않는 소비자(에디터 미리듣기)가
	// 갑자기 3D 로 들리면 안 되기 때문이다. 3D 는 SetSpatial 이 명시적으로 켠다.
	void OnSoundInitialized()
	{
		ma_sound_set_spatialization_enabled(&m_sound, MA_FALSE);
		m_initialized = true;
		m_state->RegisterChild(this);
	}

	std::shared_ptr<MiniAudioBackendState> m_state;
	ma_sound                           m_sound{};
	bool                               m_initialized = false;
	ma_sound_group*                    m_group = nullptr;
	IMiniAudioBackendChild*            m_busChild = nullptr;
	SafePtr<IAudioEffect>              m_attachedEffect;   // 단일 부착(AttachEffect)
	IMiniAudioBackendChild*            m_attachedEffectChild = nullptr;
	std::vector<SafePtr<IAudioEffect>> m_effectChain;      // 체인 부착(SetEffectChain)
	std::vector<IMiniAudioBackendChild*> m_effectChildren;
};

// ── Listener ────────────────────────────────────────────────────────────────
struct MiniAudioDeviceImpl;   // 정의는 아래 — SetMasterVolume 이 게인 합성에 쓴다.

class CMiniAudioListener final : public IAudioListener
{
public:
	CMiniAudioListener(std::shared_ptr<MiniAudioBackendState> state, MiniAudioDeviceImpl* owner)
		: m_state(std::move(state))
		, m_owner(owner)
	{
	}

	void SetPosition(AudioVec3 worldPos) override
	{
		if (ma_engine* engine = GetEngine())
		{
			ma_engine_listener_set_position(engine, 0, worldPos.X, worldPos.Y, worldPos.Z);
		}
	}

	void SetForward(AudioVec3 forwardDir) override
	{
		if (ma_engine* engine = GetEngine())
		{
			ma_engine_listener_set_direction(engine, 0, forwardDir.X, forwardDir.Y, forwardDir.Z);
		}
	}

	// ⚠ 여기서 ma_engine_set_volume 을 직접 부르면 안 된다. 프로젝트 마스터 볼륨이 같은
	//   자리를 쓰고 있어서, 매 프레임 도는 이쪽이 그걸 덮어써 무력화한다.
	//   둘은 곱해져야 하므로 Impl 이 합성해 한 번만 적용한다.
	void SetMasterVolume(float volume) override;

	// 미구현 — 호출하는 곳이 아직 없다(게임 쪽 raycast 결과를 받는 설계).
	void SetOcclusionForPlayer(SafePtr<IAudioPlayer>, float) override {}

private:
	ma_engine* GetEngine() const
	{
		return (m_state && m_state->IsOperational()) ? &m_state->Engine : nullptr;
	}

	std::shared_ptr<MiniAudioBackendState> m_state;
	MiniAudioDeviceImpl* m_owner = nullptr;
};

// ── Bus ─────────────────────────────────────────────────────────────────────
// ma_sound_group 노드를 소유. parent=null 이면 endpoint 직결(Master), 아니면 parent 로.
// player 는 이 group 으로 라우팅되어 카테고리 볼륨/뮤트가 일괄 적용된다.
class CMiniAudioBus final : public IAudioBus, public IMiniAudioBackendChild
{
public:
	CMiniAudioBus(
		std::shared_ptr<MiniAudioBackendState> state,
		EAudioBusKind kind,
		ma_sound_group* parent,
		EMiniAudioBackendChildKind childKind)
		: m_state(std::move(state))
		, m_kind(kind)
		, m_childKind(childKind)
	{
		ma_engine* engine = m_state && m_state->IsOperational() ? &m_state->Engine : nullptr;
		if (nullptr == engine) return;
		if (MA_SUCCESS == ma_sound_group_init(engine, 0, parent, &m_group))
		{
			m_initialized = true;
			m_state->RegisterChild(this);
		}
	}

	~CMiniAudioBus() override
	{
		if (m_state)
		{
			m_state->ShutdownChild(this);
		}
		ShutdownBackendResource();
	}

	EMiniAudioBackendChildKind GetBackendChildKind() const override { return m_childKind; }
	void ShutdownBackendResource() override
	{
		if (m_initialized && m_state && m_state->EngineInitialized)
		{
			ma_sound_group_uninit(&m_group);
		}
		m_initialized = false;
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
	std::shared_ptr<MiniAudioBackendState> m_state;
	EAudioBusKind   m_kind        = EAudioBusKind::Master;
	EMiniAudioBackendChildKind m_childKind = EMiniAudioBackendChildKind::CustomBus;
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
// 노드 자체는 엔진 노드 그래프에 init 되며 Player의 효과 체인에 연결된다.
class CMiniAudioEffect final : public IAudioEffect, public IMiniAudioBackendChild
{
public:
	CMiniAudioEffect(std::shared_ptr<MiniAudioBackendState> state, EAudioEffectKind kind)
		: m_state(std::move(state)), m_kind(kind)
	{
		ma_engine* engine = m_state && m_state->IsOperational() ? &m_state->Engine : nullptr;
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

		if (m_ready)
		{
			m_state->RegisterChild(this);
		}
	}

	~CMiniAudioEffect() override
	{
		if (m_state)
		{
			m_state->ShutdownChild(this);
		}
		ShutdownBackendResource();
	}

	EMiniAudioBackendChildKind GetBackendChildKind() const override
	{
		return EMiniAudioBackendChildKind::Effect;
	}
	void ShutdownBackendResource() override
	{
		if (false == m_ready) return;
		if (!m_state || false == m_state->EngineInitialized)
		{
			m_ready = false;
			m_node = nullptr;
			return;
		}
		switch (m_kind)
		{
		case EAudioEffectKind::LowPass:  ma_lpf_node_uninit(&m_lpf, nullptr);   break;
		case EAudioEffectKind::HighPass: ma_hpf_node_uninit(&m_hpf, nullptr);   break;
		case EAudioEffectKind::Echo:     ma_delay_node_uninit(&m_delay, nullptr); break;
		default:                         ma_node_uninit(&m_freeverb.Base, nullptr); break;
		}
		m_ready = false;
		m_node = nullptr;
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
				ma_lpf_config c = ma_lpf_config_init(ma_format_f32, ma_engine_get_channels(&m_state->Engine),
					ma_engine_get_sample_rate(&m_state->Engine), value, 2);
				ma_lpf_node_reinit(&c, &m_lpf);
			}
			break;
		}
		case EAudioEffectKind::HighPass:
		{
			if (key == "cutoff")
			{
				ma_hpf_config c = ma_hpf_config_init(ma_format_f32, ma_engine_get_channels(&m_state->Engine),
					ma_engine_get_sample_rate(&m_state->Engine), value, 2);
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
	MiniAudioBackendState* GetBackendState() const { return m_state.get(); }

private:
	std::shared_ptr<MiniAudioBackendState> m_state;
	EAudioEffectKind m_kind   = EAudioEffectKind::Reverb;
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
	if (false == m_initialized || !m_state || false == m_state->IsOperational()) return;
	CMiniAudioEffect* mini = dynamic_cast<CMiniAudioEffect*>(effect.TryGet());
	if (nullptr == mini || mini->GetBackendState() != m_state.get() || nullptr == mini->GetNode()) return;

	m_attachedEffect = effect;
	m_attachedEffectChild = mini;
	m_effectChain.clear();
	m_effectChildren.clear();
	RebuildEffectRouting();
}

void CMiniAudioFilePlayer::SetEffectChain(const std::vector<SafePtr<IAudioEffect>>& effects)
{
	if (false == m_initialized || !m_state || false == m_state->IsOperational()) return;

	std::vector<SafePtr<IAudioEffect>> kept;
	std::vector<IMiniAudioBackendChild*> children;
	kept.reserve(effects.size());
	children.reserve(effects.size());
	for (const SafePtr<IAudioEffect>& effect : effects)
	{
		CMiniAudioEffect* mini = dynamic_cast<CMiniAudioEffect*>(effect.TryGet());
		if (nullptr == mini || mini->GetBackendState() != m_state.get() || nullptr == mini->GetNode()) continue;
		if (children.end() != std::find(children.begin(), children.end(), mini)) continue;
		kept.push_back(effect);
		children.push_back(mini);
	}

	m_attachedEffect.Reset();
	m_attachedEffectChild = nullptr;
	m_effectChain = std::move(kept);
	m_effectChildren = std::move(children);
	RebuildEffectRouting();
}

void CMiniAudioFilePlayer::DetachAllEffects()
{
	m_attachedEffect.Reset();
	m_attachedEffectChild = nullptr;
	m_effectChain.clear();
	m_effectChildren.clear();
	RebuildEffectRouting();
}

void CMiniAudioFilePlayer::OnBackendChildInvalidating(IMiniAudioBackendChild* child)
{
	if (child == m_busChild)
	{
		m_busChild = nullptr;
		m_group = nullptr;
		RebuildEffectRouting();
		return;
	}

	if (child == m_attachedEffectChild)
	{
		m_attachedEffect.Reset();
		m_attachedEffectChild = nullptr;
		RebuildEffectRouting();
		return;
	}

	bool removed = false;
	for (std::size_t i = 0; i < m_effectChildren.size();)
	{
		if (m_effectChildren[i] == child)
		{
			m_effectChildren.erase(m_effectChildren.begin() + static_cast<std::ptrdiff_t>(i));
			m_effectChain.erase(m_effectChain.begin() + static_cast<std::ptrdiff_t>(i));
			removed = true;
			continue;
		}
		++i;
	}
	if (removed)
	{
		RebuildEffectRouting();
	}
}

void CMiniAudioFilePlayer::RebuildEffectRouting()
{
	if (false == m_initialized || !m_state || false == m_state->EngineInitialized)
	{
		return;
	}

	ma_node* output = GetOutputNode();
	if (nullptr == output)
	{
		return;
	}

	std::vector<ma_node*> nodes;
	if (nullptr != m_attachedEffectChild)
	{
		CMiniAudioEffect* effect = static_cast<CMiniAudioEffect*>(m_attachedEffectChild);
		if (ma_node* node = effect->GetNode())
		{
			nodes.push_back(node);
		}
	}
	else
	{
		nodes.reserve(m_effectChildren.size());
		for (IMiniAudioBackendChild* child : m_effectChildren)
		{
			CMiniAudioEffect* effect = static_cast<CMiniAudioEffect*>(child);
			if (ma_node* node = effect->GetNode())
			{
				nodes.push_back(node);
			}
		}
	}

	if (nodes.empty())
	{
		ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&m_sound), 0, output, 0);
		return;
	}

	ma_node_attach_output_bus(nodes.back(), 0, output, 0);
	for (std::size_t i = nodes.size() - 1; i > 0; --i)
	{
		ma_node_attach_output_bus(nodes[i - 1], 0, nodes[i], 0);
	}
	ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&m_sound), 0, nodes.front(), 0);
}

// ── Impl ──────────────────────────────────────────────────────────────────
struct MiniAudioDeviceImpl
{
	std::shared_ptr<MiniAudioBackendState> Backend;
	OwnerPtr<CMiniAudioListener>    Listener;
	// 표준 믹싱 버스 — Master(endpoint 직결) ← Music/SFX/Voice/UI/Custom.
	OwnerPtr<CMiniAudioBus>         Buses[AUDIO_BUS_KIND_COUNT];
	// 엔진 볼륨 자리를 둘이 나눠 쓴다 — 프로젝트 설정과 AudioListener 컴포넌트.
	// 한쪽이 직접 ma_engine_set_volume 을 부르면 다른 쪽을 덮어쓰므로 여기서 곱해 적용한다.
	float                           MasterVolume = 1.0f;   // 프로젝트 전역
	float                           ListenerGain = 1.0f;   // AudioListener.MasterVolume

	void ApplyEngineVolume()
	{
		if (Backend && Backend->IsOperational())
		{
			ma_engine_set_volume(&Backend->Engine, MasterVolume * ListenerGain);
		}
	}

	CMiniAudioBus* GetBusPtr(EAudioBusKind kind)
	{
		const std::size_t i = static_cast<std::size_t>(kind);
		return (i < AUDIO_BUS_KIND_COUNT) ? Buses[i].Get() : nullptr;
	}
};

CMiniAudioDevice::CMiniAudioDevice()  = default;
CMiniAudioDevice::~CMiniAudioDevice()
{
	Finalize();
}

bool CMiniAudioDevice::Initialize(const AudioDeviceDesc& desc)
{
	// Initialize/Finalize 경계를 호출자가 명시적으로 관리한다. 이미 활성인 장치를
	// 암묵적으로 재시작하면 외부 Player/Effect가 죽은 node graph를 참조할 수 있다.
	if (m_impl)
	{
		return false;
	}

	m_impl = MakeOwnerPtr<MiniAudioDeviceImpl>();
	if (!m_impl) return false;
	m_impl->Backend = std::make_shared<MiniAudioBackendState>();
	if (false == static_cast<bool>(m_impl->Backend))
	{
		m_impl.Reset();
		return false;
	}

	ma_engine_config cfg = ma_engine_config_init();
	cfg.sampleRate = desc.Format.SampleRate;
	cfg.channels   = desc.Format.Channels;

	if (MA_SUCCESS != ma_engine_init(&cfg, &m_impl->Backend->Engine))
	{
		m_impl.Reset();
		return false;
	}
	m_impl->Backend->EngineInitialized = true;
	m_impl->Listener = MakeOwnerPtr<CMiniAudioListener>(m_impl->Backend, m_impl.Get());

	// 표준 믹싱 버스 계층 구성 — Master 는 endpoint 직결(parent=null),
	// 나머지는 Master 의 group 을 parent 로 둬 카테고리 볼륨이 Master 로 합쳐진다.
	{
		const std::size_t masterIdx = static_cast<std::size_t>(EAudioBusKind::Master);
		m_impl->Buses[masterIdx] = MakeOwnerPtr<CMiniAudioBus>(
			m_impl->Backend, EAudioBusKind::Master, nullptr, EMiniAudioBackendChildKind::MasterBus);
		ma_sound_group* masterGroup = m_impl->Buses[masterIdx] ? m_impl->Buses[masterIdx]->GetGroup() : nullptr;

		for (std::size_t i = 0; i < AUDIO_BUS_KIND_COUNT; ++i)
		{
			if (i == masterIdx) continue;
			m_impl->Buses[i] = MakeOwnerPtr<CMiniAudioBus>(
				m_impl->Backend,
				static_cast<EAudioBusKind>(i),
				masterGroup,
				EMiniAudioBackendChildKind::StandardBus);
		}
	}
	return true;
}

void CMiniAudioDevice::Finalize()
{
	if (m_impl)
	{
		if (m_impl->Backend)
		{
			m_impl->Backend->Finalize();
		}
		for (auto& bus : m_impl->Buses) bus.Reset();
		m_impl->Listener.Reset();
		m_impl->Backend.reset();
		m_impl.Reset();
	}
}

void CMiniAudioDevice::Tick(float)
{
	// PR A 단계에서는 별다른 일 없음 — 단계 B 이후 ended player GC, marker
	// dispatch 등을 여기 채움.
}

OwnerPtr<IAudioPlayer> CMiniAudioDevice::CreatePlayer(const AudioPlayerDesc& desc)
{
	if (nullptr != desc.StreamPathUtf8)
	{
		// desc.Bus 는 라우팅 대상 버스다. 예전엔 여기서 버려져 무엇을 지정하든 Master 로
		// 갔다 — 카테고리 볼륨이 동작하지 않던 원인.
		const EAudioBusKind kind = desc.Bus.IsValid() ? desc.Bus->GetKind() : EAudioBusKind::Master;
		return CreatePlayerFromFile(desc.StreamPathUtf8, kind);
	}

	// PCM 기반 생성은 아직 구현 전이다. 경로도 PCM도 없는 기존 호출에는 안전한
	// 빈 player를 유지하되, 파일 로딩 실패는 아래 경로에서 null로 명확히 구분한다.
	return MakeOwnerPtr<CMiniAudioPlayerStub>();
}

OwnerPtr<IAudioPlayer> CMiniAudioDevice::CreatePlayerFromFile(const char* filePathUtf8, EAudioBusKind bus)
{
	if (!m_impl || !m_impl->Backend || false == m_impl->Backend->IsOperational() || nullptr == filePathUtf8)
	{
		return nullptr;
	}
	// 지정 버스로 라우팅. 버스 미초기화 시 group=null → endpoint 직결(기존 동작).
	CMiniAudioBus* busPtr = m_impl->GetBusPtr(bus);
	ma_sound_group* group = busPtr ? busPtr->GetGroup() : nullptr;

	OwnerPtr<CMiniAudioFilePlayer> player = MakeOwnerPtr<CMiniAudioFilePlayer>(
		m_impl->Backend, filePathUtf8, group, busPtr);
	if (false == player->IsInitialized())
	{
		return nullptr;
	}
	return player;
}

OwnerPtr<IAudioBus> CMiniAudioDevice::CreateBus(EAudioBusKind kind)
{
	// Custom 등 표준 외 버스 — Master 하위에 새 group 생성.
	CMiniAudioBus* master = m_impl ? m_impl->GetBusPtr(EAudioBusKind::Master) : nullptr;
	ma_sound_group* parent = master ? master->GetGroup() : nullptr;
	return MakeOwnerPtr<CMiniAudioBus>(
		m_impl ? m_impl->Backend : std::shared_ptr<MiniAudioBackendState>{},
		kind,
		parent,
		EMiniAudioBackendChildKind::CustomBus);
}

SafePtr<IAudioBus> CMiniAudioDevice::GetBus(EAudioBusKind kind)
{
	if (!m_impl) return SafePtr<IAudioBus>();
	const std::size_t i = static_cast<std::size_t>(kind);
	if (i >= AUDIO_BUS_KIND_COUNT) return SafePtr<IAudioBus>();
	return m_impl->Buses[i] ? m_impl->Buses[i].GetSafePtr() : SafePtr<IAudioBus>();
}

OwnerPtr<IAudioEffect> CMiniAudioDevice::CreateEffect(EAudioEffectKind kind)
{
	return MakeOwnerPtr<CMiniAudioEffect>(
		m_impl ? m_impl->Backend : std::shared_ptr<MiniAudioBackendState>{}, kind);
}

SafePtr<IAudioListener> CMiniAudioDevice::GetPrimaryListener()
{
	return (m_impl && m_impl->Listener) ? m_impl->Listener.GetSafePtr() : SafePtr<IAudioListener>();
}

double CMiniAudioDevice::GetGlobalAudioTimeSeconds() const
{
	if (!m_impl || !m_impl->Backend || false == m_impl->Backend->IsOperational()) return 0.0;
	return static_cast<double>(ma_engine_get_time_in_milliseconds(&m_impl->Backend->Engine)) / 1000.0;
}

double CMiniAudioDevice::GetOutputLatencySeconds() const
{
	if (!m_impl || !m_impl->Backend || false == m_impl->Backend->IsOperational()) return 0.0;
	ma_device* device = ma_engine_get_device(&m_impl->Backend->Engine);
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
	m_impl->ApplyEngineVolume();
}

void CMiniAudioListener::SetMasterVolume(float volume)
{
	if (nullptr == m_owner) return;
	m_owner->ListenerGain = volume;
	m_owner->ApplyEngineVolume();
}

float CMiniAudioDevice::GetMasterVolume() const
{
	return m_impl ? m_impl->MasterVolume : 1.0f;
}

#endif // JBRO_HAS_MINIAUDIO
