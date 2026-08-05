#pragma once

#include "Core/Asset/AssetTypes.h"   // AssetGuid
#include "Core/Audio/AudioTypes.h"   // EAudioAttenuationModel
#include "GameFramework/Component/Component.h"

#include "Utillity/Types/Array.h"
#include "Utillity/Types/String.h"   // Bus — 버스는 이름으로 지목한다

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  AudioListener / AudioPlayer 컴포넌트
//
//  AudioListener
//    - 캔버스에 0~1 개. 여러 개가 있으면 CAudioSystem 이 첫 번째 활성 인스턴스만 사용.
//    - Transform2D 의 월드 위치가 IAudioDevice 의 primary listener 로 매 프레임 push.
//
//  AudioPlayer
//    - 사운드 자산 GUID 를 보유.
//    - 컴포넌트 활성 + PlayOnStart 이면 시스템이 IAudioPlayer 인스턴스 생성·재생.
//    - 자산 임포트 옵션의 Loop/3D/Volume 은 자산의 기본값.
//      컴포넌트의 동명 필드가 매 프레임 player 에 적용되므로 인스턴스별 오버라이드가 된다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class AudioListener final : public CComponent
{
	JBRO_COMPONENT(AudioListener)
public:
	float MasterVolume = 1.0f;   // 청취자 전체 게인 (Project Audio Master 와 곱).
};

class AudioPlayer final : public CComponent
{
	JBRO_COMPONENT(AudioPlayer)
public:
	// 재생할 사운드 자산.
	AssetGuid AudioGuid;

	// 라우팅될 믹싱 버스 **이름**. 목록은 프로젝트 세팅에서 편집한다(입력 레이어와 같은 방식).
	// 카테고리 볼륨(옵션 화면의 BGM/SFX 슬라이더)은 이 버스 단위로 걸린다 —
	// 스크립트에서 `Script.Audio->GetBus("Music")->SetVolume(v)`.
	// 버스는 전부 Master 하위라 Master 볼륨이 그대로 위에 곱해진다.
	//
	// 기본값이 "SFX" 인 건 배치되는 AudioPlayer 대부분이 효과음이기 때문이다. 버스를
	// 저작하지 않은 기존 씬은 이 기본값으로 읽히는데, SFX 버스 초기 볼륨이 1.0 이고
	// Master 하위이므로 **들리는 결과는 예전(Master 직결)과 같다**.
	// 빈 문자열이거나 목록에 없는 이름이면 Master 로 떨어지고 경고가 한 번 남는다.
	String Bus = "SFX";

	// DSP 효과 에셋(reverb 등) 체인. 리스트 순서대로 적용된다:
	//   sound -> EffectGuids[0] -> EffectGuids[1] -> ... -> endpoint.
	// 비어 있으면 효과 없음. 런타임 효과 노드 캐시/strong ref 는 CAudioSystem 이
	// per-instance 로 보유한다(컴포넌트는 순수 데이터만).
	Array<AssetGuid> EffectGuids;

	// ── 인스턴스 단위 오버라이드 ──────────────────────────────────────────
	// 임포트 옵션의 동명 필드를 인스턴스 단위로 덮어쓴다.
	float     Volume      = 1.0f;
	float     Pitch       = 1.0f;
	bool      Loop        = false;
	bool      Is3D        = false;
	float     MinDistance = 1.0f;
	float     MaxDistance = 50.0f;
	// 아래 둘은 Is3D 일 때만 의미가 있다.
	EAudioAttenuationModel AttenuationModel = EAudioAttenuationModel::Inverse;
	float     Rolloff     = 1.0f;

	// 컴포넌트가 활성화될 때(또는 캔버스 시작 시) 자동 재생.
	bool      PlayOnStart = true;
};
