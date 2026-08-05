#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

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

// ── 믹싱 버스 ───────────────────────────────────────────────────────────────
// 게임 측에서 카테고리별 볼륨 조절 / 효과 부착 단위. **버스는 이름(문자열)으로 지목한다.**
//
// enum 이 아닌 이유: 어떤 카테고리가 필요한지는 게임마다 다르다. enum 이면 엔진이 미리
// 정한 목록 밖으로 못 나가고, 안 쓰는 항목이 인스펙터에 남는다. 입력 레이어
// (ProjectInfo::InputLayers)와 같은 방식으로 프로젝트 세팅에서 목록을 편집한다.
//
// 규칙:
//  · "Master" 는 예약 이름 — 항상 존재하는 루트 버스이고 나머지는 전부 그 자식이다.
//    그래서 Master 볼륨은 모든 소리에 곱해진다.
//  · 빈 이름은 Master 로 해석한다(= 버스 미지정).
//  · 목록에 없는 이름으로 라우팅을 요청하면 Master 로 떨어지고 경고가 한 번 남는다.
inline constexpr const char* AUDIO_MASTER_BUS_NAME = "Master";

// 한 프로젝트가 가질 수 있는 버스 수 상한. 버스는 사람이 손으로 관리하는 카테고리라
// 이 정도면 넉넉하고, 조회가 선형 탐색이라 상한이 있는 편이 낫다.
inline constexpr std::size_t MAX_AUDIO_BUSES = 16;

// 버스 이름 한 개의 최대 길이(null 포함). 백엔드가 POD 버퍼로 들고 있는다.
inline constexpr std::size_t AUDIO_BUS_NAME_CAPACITY = 64;

// 이름 없음/빈 이름은 Master 로 본다. 라우팅 판정을 한 곳으로 모아 두면
// "빈 문자열이면 어디로 가지?" 를 호출부마다 다시 정하지 않아도 된다.
inline const char* ResolveAudioBusName(const char* name)
{
	return (nullptr == name || '\0' == name[0]) ? AUDIO_MASTER_BUS_NAME : name;
}

// 버스 이름을 고정 버퍼로 복사한다(넘치면 자르고 항상 null 종료).
// 백엔드는 호출자(프로젝트 세팅 벡터)의 수명과 무관하게 이름을 들고 있어야 한다.
inline void CopyAudioBusName(char (&destination)[AUDIO_BUS_NAME_CAPACITY], const char* name)
{
	const char* source = ResolveAudioBusName(name);
	std::size_t i = 0;
	for (; i + 1 < AUDIO_BUS_NAME_CAPACITY && '\0' != source[i]; ++i)
	{
		destination[i] = source[i];
	}
	destination[i] = '\0';
}

// ── 버스 정의(저작/설정 측) ─────────────────────────────────────────────────
// 프로젝트 세팅이 편집하고 .jproject / BuildManifest 를 거쳐 디바이스로 들어간다.
// InputActionDef 와 같은 위치의 타입이다 — 위쪽 POD 들과 달리 STL 을 쓰지만, 이건
// 매 프레임 경로가 아니라 **로드 시점에 한 번** 도는 설정 데이터라 문제되지 않는다.
struct AudioBusDef
{
	std::string Name;
	// 이 버스의 시작 음량. 스크립트가 SetVolume 으로 바꾸면 그쪽이 이긴다
	// (옵션 화면이 저장된 값을 불러와 덮어쓰는 것이 정상 흐름이다).
	float       Volume = 1.0f;
};

// 버스 이름 비교. 양쪽 다 빈 이름/nullptr 을 Master 로 정규화한 뒤 비교한다.
inline bool IsSameAudioBusName(const char* lhs, const char* rhs)
{
	const char* a = ResolveAudioBusName(lhs);
	const char* b = ResolveAudioBusName(rhs);
	for (std::size_t i = 0; i < AUDIO_BUS_NAME_CAPACITY; ++i)
	{
		if (a[i] != b[i])
		{
			return false;
		}
		if ('\0' == a[i])
		{
			return true;
		}
	}
	return true;
}

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
