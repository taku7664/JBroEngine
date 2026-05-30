#pragma once

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
enum class EAudioBusKind : std::uint8_t
{
	Master,
	Music,
	SFX,
	Voice,
	UI,
	Custom,
	Count,   // enum 끝 sentinel (배열 크기용)
};

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
struct AudioSpatialParams
{
	bool  Is3D        = false;
	float MinDistance = 1.0f;
	float MaxDistance = 50.0f;
	// 향후 occlusion: float OcclusionAttenuation = 1.0f; ...
};

// ── 디바이스 디스크립터 ─────────────────────────────────────────────────────
struct AudioDeviceDesc
{
	AudioFormatInfo Format;
	std::uint32_t   MaxPolyphony = 64;   // 동시 재생 가능한 player 상한
};
