#pragma once

#include "Core/Audio/AudioTypes.h"
#include "Utillity/Pointer/SafePtr.h"

class IAudioEffect;

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  IAudioBus ─ 믹싱 버스 (Master / Music / SFX / Voice / UI / Custom)
//
//  Player 가 Bus 에 라우팅되고, Bus 는 자기에게 부착된 효과 체인을 거쳐
//  최종 출력 또는 부모 Bus 로 향한다.
//
//  버스는 **이름으로 지목한다** — 목록은 프로젝트 세팅이 정한다(AudioTypes.h 참조).
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
class IAudioBus : public EnableSafeFromThis<IAudioBus>
{
public:
	virtual ~IAudioBus() = default;

	// null 이 아닌 null-종료 문자열. 디바이스가 살아 있는 동안 유효하다.
	virtual const char* GetName() const = 0;

	virtual void  SetVolume(float volume) = 0;
	virtual float GetVolume() const = 0;
	virtual void  SetMuted (bool muted) = 0;
	virtual bool  IsMuted  () const = 0;

	// 향후 DSP 효과
	virtual void  AttachEffect(SafePtr<IAudioEffect> effect) = 0;
	virtual void  DetachAllEffects() = 0;
};
