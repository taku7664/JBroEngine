#pragma once

#include <cstddef>
#include <cstdint>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  AudioTypes.h ─ 사운드 시스템 공통 타입
//
//  IAudioDevice / IAudioPlayer / IAudioListener / IAudioBus / IAudioEffect
//  인터페이스가 모두 의존하는 작은 enum / struct 정의 집합.
//  cpp 의존성을 최소화하기 위해 평범한 POD 만 둔다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// ── 샘플 포맷 ────────────────────────────────────────────────────────────────
enum class EAudioFormat : std::uint8_t
{
	PCM_S16,    // 16-bit signed PCM (CD 품질 SFX 의 일반적 선택)
	PCM_F32,    // 32-bit float (믹싱/효과에 유리, miniaudio 내부 기본값)
};

// ── 임포트 모드 ────────────────────────────────────────────────────────────
// 짧은 효과음은 디코딩해서 메모리에, 긴 BGM 은 스트리밍.
enum class EAudioImportMode : std::uint8_t
{
	Decompressed,
	Streaming,
};

// ── 믹싱 버스 종류 ──────────────────────────────────────────────────────────
// 게임 측에서 카테고리별 볼륨 조절 / 효과 부착 단위.
//
// **끝 sentinel(Count)을 두지 않는다.** 이 enum 은 AudioPlayer 컴포넌트의 프로퍼티라
// 인스펙터 드롭다운에 그대로 뜨는데, sentinel 이 있으면 "Count" 라는 고를 수 있지만
// 아무 데도 라우팅되지 않는 항목이 생긴다. 배열 크기는 아래 상수를 쓴다.
enum class EAudioBusKind : std::uint8_t
{
	Master,
	Music,
	SFX,
	Voice,
	UI,
	Custom,
};

// 버스 배열 크기. 마지막 항목에서 유도하므로 enum 에 항목을 추가하면 자동으로 따라온다
// (새 항목은 반드시 Custom **앞에** 넣을 것).
inline constexpr std::size_t AUDIO_BUS_KIND_COUNT =
	static_cast<std::size_t>(EAudioBusKind::Custom) + 1;

// ── DSP 효과 종류 (향후 확장) ──────────────────────────────────────────────
enum class EAudioEffectKind : std::uint8_t
{
	Reverb,
	LowPass,
	HighPass,
	Echo,
	Distortion,
	Compressor,
	Limiter,
};

// ── 포맷 정보 ───────────────────────────────────────────────────────────────
struct AudioFormatInfo
{
	EAudioFormat  Format     = EAudioFormat::PCM_F32;
	std::uint32_t SampleRate = 48000;
	std::uint16_t Channels   = 2;
};

// ── 3D 좌표 — 2D 엔진이지만 backend 가 3D vec3 요구하므로 Z 는 보통 0 ─────
struct AudioVec3
{
	float X = 0.0f, Y = 0.0f, Z = 0.0f;
};

// ── 3D / 공간음향 파라미터 ──────────────────────────────────────────────────
// 거리에 따라 소리가 줄어드는 곡선. miniaudio 의 ma_attenuation_model 과 1:1 대응한다.
enum class EAudioAttenuationModel : std::uint8_t
{
	None,         // 거리 무관 — 방향(팬) 만 적용된다.
	Inverse,      // 기본값. 현실의 역제곱에 가장 가깝다.
	Linear,       // MinDistance~MaxDistance 사이를 직선으로 — 게임에서 다루기 쉽다.
	Exponential,  // 가까이서 급격히 줄어든다.
};

struct AudioSpatialParams
{
	bool  Is3D        = false;
	float MinDistance = 1.0f;   // 이 거리 안에서는 감쇠 없음.
	float MaxDistance = 50.0f;  // 이 거리 밖에서는 더 줄지 않음.
	EAudioAttenuationModel AttenuationModel = EAudioAttenuationModel::Inverse;
	float Rolloff     = 1.0f;   // 곡선의 가파르기. 클수록 빨리 작아진다.
	// 향후 occlusion: float OcclusionAttenuation = 1.0f; ...
};

// ── 디바이스 디스크립터 ─────────────────────────────────────────────────────
struct AudioDeviceDesc
{
	AudioFormatInfo Format;
	std::uint32_t   MaxPolyphony = 64;   // 동시 재생 가능한 player 상한
};
