#pragma once

#include "Core/Asset/AssetTypes.h"   // AssetGuid

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  AudioListener / AudioPlayer 컴포넌트
//
//  AudioListener
//    - 씬에 0~1 개. 여러 개가 있으면 CAudioSystem 이 첫 번째 활성 인스턴스만 사용.
//    - Transform2D 의 월드 위치가 IAudioDevice 의 primary listener 로 매 프레임 push.
//
//  AudioPlayer
//    - 사운드 자산 GUID 를 보유.
//    - 컴포넌트 활성 + PlayOnStart 이면 시스템이 IAudioPlayer 인스턴스 생성·재생.
//    - 자산 임포트 옵션의 Loop/3D/Volume 은 자산의 기본값.
//      컴포넌트의 동명 필드가 매 프레임 player 에 적용되므로 인스턴스별 오버라이드가 된다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct AudioListener
{
	bool  IsEnabled    = true;
	float MasterVolume = 1.0f;   // 청취자 전체 게인 (Project Audio Master 와 곱).
};

struct AudioPlayer
{
	bool      IsEnabled = true;

	// 재생할 사운드 자산.
	AssetGuid AudioGuid;

	// ── 인스턴스 단위 오버라이드 ──────────────────────────────────────────
	// 임포트 옵션의 동명 필드를 인스턴스 단위로 덮어쓴다.
	float     Volume      = 1.0f;
	float     Pitch       = 1.0f;
	bool      Loop        = false;
	bool      Is3D        = false;
	float     MinDistance = 1.0f;
	float     MaxDistance = 50.0f;

	// 컴포넌트가 활성화될 때(또는 씬 시작 시) 자동 재생.
	bool      PlayOnStart = true;
};
