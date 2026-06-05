#pragma once

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
};

extern RuntimeConfig Runtime;
