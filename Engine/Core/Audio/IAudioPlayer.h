#pragma once

#include "Core/Audio/AudioTypes.h"
#include "Utillity/SafePtr.h"

#include <cstdint>

class IAudioEffect;

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  IAudioPlayer ─ 한 개의 재생 인스턴스
//
//  한 자산을 여러 번 동시에 울리려면 IAudioDevice::CreatePlayer 로 인스턴스
//  여러 개를 만든다.  컴포넌트 한 개는 인스턴스 한 개를 보유한다.
//
//  리듬게임 같은 정밀 싱크용 메서드(PlayAt / GetPositionFrames / Seek) 도
//  여기 노출된다.  backend 가 sample 단위 정확도로 처리.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
class IAudioPlayer : public EnableSafeFromThis<IAudioPlayer>
{
public:
	virtual ~IAudioPlayer() = default;

public:
	// ── 트랜스포트 ─────────────────────────────────────────────────────────
	virtual void Play()  = 0;
	virtual void Pause() = 0;
	virtual void Stop()  = 0;
	virtual bool IsPlaying() const = 0;
	// non-loop 자산이 끝까지 재생되었는지 — GC 판단용
	virtual bool IsEnded()   const = 0;

	// ── 정밀 싱크 ──────────────────────────────────────────────────────────
	// 오디오 클락의 특정 시점에 재생 시작 (샘플 단위 정확).
	// 리듬게임에서 박자 그리드에 맞춰 시작할 때 사용.
	virtual void PlayAt(double scheduledAudioTimeSeconds) = 0;

	// 현재 재생 위치 — 노트 판정용. PCM frame 단위가 가장 정확.
	virtual std::uint64_t GetPositionFrames()  const = 0;
	virtual double        GetPositionSeconds() const = 0;
	virtual double        GetDurationSeconds() const = 0;

	// 특정 frame 으로 점프
	virtual void Seek(std::uint64_t frame) = 0;
	// 특정 초 위치로 점프. 기본 구현은 sample rate 가정으로 Seek 호출 — 정확한 변환은 backend 가 override.
	virtual void SeekSeconds(double seconds)
	{
		const double dur = GetDurationSeconds();
		if (dur <= 0.0) return;
		const std::uint64_t curFrame = GetPositionFrames();
		const double        curSec   = GetPositionSeconds();
		if (curSec > 0.0 && curFrame > 0)
		{
			const double framesPerSec = static_cast<double>(curFrame) / curSec;
			Seek(static_cast<std::uint64_t>(seconds * framesPerSec));
		}
	}

	// ── 볼륨 / 피치 / 루프 ─────────────────────────────────────────────────
	virtual void SetVolume(float volume) = 0;
	virtual void SetPitch (float pitch ) = 0;
	virtual void SetLoop  (bool  loop  ) = 0;

	// ── 공간 음향 ──────────────────────────────────────────────────────────
	virtual void SetPosition(AudioVec3 worldPos)              = 0;
	virtual void SetSpatial (const AudioSpatialParams& params) = 0;

	// ── DSP 효과 체인 (향후) ───────────────────────────────────────────────
	virtual void AttachEffect(SafePtr<IAudioEffect> effect) = 0;
	virtual void DetachAllEffects() = 0;
};

// ── Player 생성 디스크립터 ────────────────────────────────────────────────
class IAudioBus;
struct AudioPlayerDesc
{
	// Decompressed 모드 — 디코딩된 PCM
	const void*        PcmData      = nullptr;
	std::size_t        PcmByteCount = 0;

	// Streaming 모드 — 파일 경로 (UTF-8). 사용 시 PcmData 는 무시.
	const char*        StreamPathUtf8 = nullptr;

	EAudioImportMode   Mode = EAudioImportMode::Decompressed;
	AudioFormatInfo    Format;

	SafePtr<IAudioBus> Bus;   // null 이면 Master 로 라우팅
};
