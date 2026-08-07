#pragma once

#include "Core/Asset/AssetTypes.h"

#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  RuntimeConfig — 호스트 런타임의 전역 *설정값/상태* 묶음.
//
//  · EngineCore 는 서비스 포인터(SafePtr<X>) 전용 번들이다. PPU 같은 스칼라 설정값,
//    포커스 같은 상태 플래그는 성격이 달라 여기 모은다(번들 균질성 유지).
//  · 호스트 전용이다(EngineCore 와 동일). DLL 경계를 넘지 않는다 — 게임 DLL 안 코드는
//    이 값을 직접 읽지 않고, 호스트가 시스템 생성 시 주입한다(예: SpriteRenderSystem).
//  · 에디터·빌드 게임의 공통 주입 합류점:
//      에디터  : ProjectManager 가 LoadProject / 설정 Apply 시 갱신
//      빌드게임: 부팅 시 manifest 값으로 1회 주입
// ─────────────────────────────────────────────────────────────────────────────
struct RuntimeConfig
{
	// 프로젝트 Default PPU 의 런타임 캐시. 자산별 PPU 가 0(미지정) 일 때 폴백으로 사용.
	float PixelsPerUnit = 100.0f;

	// 프로젝트 해상도(픽셀)의 런타임 캐시 = **화면 공간 레이어의 저작 기준 렉트**.
	// PPU 로 나눠 유닛 렉트가 된다(1920x1080 @ 100 → 19.2 x 10.8).
	//
	// 실제 렌더 표면 크기와는 다른 값이다. 표면은 창 리사이즈·기기 종횡비로 달라지고,
	// 그때 이 기준과 표면의 차이를 어떻게 메울지는 레이어의 EScreenScaleMode 가 정한다.
	// 표면 크기를 그대로 기준으로 삼으면 저작 좌표가 기기마다 달라진다.
	float ReferenceResolutionWidth  = 1920.0f;
	float ReferenceResolutionHeight = 1080.0f;
	AssetGuid DefaultFontFamilyGuid = INVALID_ASSET_GUID;
	std::vector<AssetGuid> FallbackFontFamilies;

	// 2D 라이팅 전역 앰비언트(RGBA). LightMap 을 이 값으로 클리어한 뒤 Light2D 를 가산 누적한다.
	// 라이트가 하나도 없는 캔버스는 라이팅 패스 자체가 컬링돼 이 값과 무관하게 화면 불변(패스스루).
	// 라이트가 있으면 캔버스가 이 밝기로 깔리고 라이트가 표면을 밝힌다 → 어두워야 라이트가 보인다.
	// 기본 0.2(어두운 앰비언트). 차후 프로젝트 설정으로 노출 예정.
	float AmbientLight[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
};

extern RuntimeConfig Runtime;
