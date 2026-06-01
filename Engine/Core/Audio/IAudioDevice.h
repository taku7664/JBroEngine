#pragma once

#include "Core/Audio/AudioTypes.h"
#include "Core/Audio/IAudioPlayer.h"   // AudioPlayerDesc
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <functional>

class IAudioBus;
class IAudioEffect;
class IAudioListener;

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  IAudioDevice ─ 사운드 백엔드 추상화
//
//  RHI 의 IRHIDevice 와 같은 역할.  현재 단일 구현(CMiniAudioDevice)이지만
//  필요 시 다른 백엔드(예: 네이티브 WASAPI, 네이티브 Web Audio API) 로 교체 가능.
//
//  미니오디오 가 자체적으로 거의 모든 플랫폼(Windows/macOS/Linux/Android/iOS/Web)
//  을 지원하므로 1차 구현은 단일 백엔드로 충분하다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
class IAudioDevice : public EnableSafeFromThis<IAudioDevice>
{
public:
	virtual ~IAudioDevice() = default;

public:
	virtual bool Initialize(const AudioDeviceDesc& desc) = 0;
	virtual void Finalize() = 0;

	// 매 프레임 — ended 콜백 처리, listener/player 위치 push, 등.
	virtual void Tick(float deltaSeconds) = 0;

	// ── 생성 ───────────────────────────────────────────────────────────────
	virtual OwnerPtr<IAudioPlayer>  CreatePlayer (const AudioPlayerDesc& desc) = 0;
	virtual OwnerPtr<IAudioBus>     CreateBus    (EAudioBusKind kind) = 0;
	virtual OwnerPtr<IAudioEffect>  CreateEffect (EAudioEffectKind kind) = 0;

	// 씬당 활성 1개 (backend 가 소유). 게임 측 AudioListener 컴포넌트가
	// SetPosition 등으로 매 프레임 갱신.
	virtual SafePtr<IAudioListener> GetPrimaryListener() = 0;

	// ── 정밀 싱크 (리듬게임 등) ────────────────────────────────────────────
	// 오디오 콜백 기준 단조 증가 시계 — 게임 frame rate 와 무관하게 일정.
	virtual double GetGlobalAudioTimeSeconds() const = 0;
	// 디바이스 출력 지연 — 사용자가 실제 듣는 시점 = GlobalAudioTime - OutputLatency.
	virtual double GetOutputLatencySeconds()  const = 0;

	// 재생 중 특정 frame 도달 시 콜백.  콜백은 오디오 콜백 스레드에서 호출될
	// 수 있으므로 호출자가 game-thread queue 로 dispatch 하는 정책 적용 권장.
	virtual void RegisterPlayerMarker(SafePtr<IAudioPlayer> player,
	                                  std::uint64_t frame,
	                                  std::function<void()> callback) = 0;

	// ── 글로벌 볼륨 ────────────────────────────────────────────────────────
	virtual void  SetMasterVolume(float volume) = 0;
	virtual float GetMasterVolume() const = 0;
};
