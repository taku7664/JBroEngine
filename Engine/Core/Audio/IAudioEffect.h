#pragma once

#include "Core/Audio/AudioTypes.h"
#include "Utillity/SafePtr.h"

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  IAudioEffect ─ DSP 효과 (reverb / LP / HP / compressor / ...)
//
//  파라미터는 문자열 키 + float 의 단순한 dict — DSP 노드 그래프 표현 자유도.
//  IAudioPlayer 또는 IAudioBus 에 부착될 수 있다.  단계 G 의 본격 구현 대상.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
class IAudioEffect : public EnableSafeFromThis<IAudioEffect>
{
public:
	virtual ~IAudioEffect() = default;

	virtual EAudioEffectKind GetKind() const = 0;

	virtual void  SetParameter(const char* name, float value) = 0;
	virtual float GetParameter(const char* name) const = 0;
};
