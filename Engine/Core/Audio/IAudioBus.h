#pragma once

#include "Core/Audio/AudioTypes.h"
#include "Utillity/Pointer/SafePtr.h"

class IAudioEffect;

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  IAudioBus ─ 믹싱 버스 (Master / Music / SFX / Voice / UI / Custom)
//
//  Player 가 Bus 에 라우팅되고, Bus 는 자기에게 부착된 효과 체인을 거쳐
//  최종 출력 또는 부모 Bus 로 향한다.  현재는 인터페이스만 노출되고
//  단계 G 에서 본격 사용.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
class IAudioBus : public EnableSafeFromThis<IAudioBus>
{
public:
	virtual ~IAudioBus() = default;

	virtual EAudioBusKind GetKind() const = 0;

	virtual void  SetVolume(float volume) = 0;
	virtual float GetVolume() const = 0;
	virtual void  SetMuted (bool muted) = 0;
	virtual bool  IsMuted  () const = 0;

	// 향후 DSP 효과
	virtual void  AttachEffect(SafePtr<IAudioEffect> effect) = 0;
	virtual void  DetachAllEffects() = 0;
};
