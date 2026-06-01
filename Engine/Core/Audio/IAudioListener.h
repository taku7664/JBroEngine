#pragma once

#include "Core/Audio/AudioTypes.h"
#include "Utillity/Pointer/SafePtr.h"

class IAudioPlayer;

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  IAudioListener ─ 씬당 활성 1개 — 보통 Camera2D 의 엔티티에 부착된 컴포넌트가
//  관리한다.  position / forward / 마스터 볼륨을 backend 로 push.
//
//  향후 occlusion 은 게임 측에서 raycast 결과를 SetOcclusionForPlayer 로 넘긴다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
class IAudioListener : public EnableSafeFromThis<IAudioListener>
{
public:
	virtual ~IAudioListener() = default;

	virtual void SetPosition    (AudioVec3 worldPos)  = 0;
	virtual void SetForward     (AudioVec3 forwardDir) = 0;
	virtual void SetMasterVolume(float volume)        = 0;

	// 향후 occlusion — 게임 측 raycast 결과 push.
	// attenuation: 1.0 = 차폐 없음, 0.0 = 완전 차폐.
	virtual void SetOcclusionForPlayer(SafePtr<IAudioPlayer> player, float attenuation) = 0;
};
