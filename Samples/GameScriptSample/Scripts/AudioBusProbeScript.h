#pragma once

#include "GameFramework/Scripting/ScriptAPI.h"

// ── CAudioBusProbeScript ──────────────────────────────────────────────────────
// 오디오 버스 라우팅 자가검증 스크립트.
//
// 확인하려는 것: AudioPlayer 컴포넌트의 Bus 프로퍼티가 실제로 그 버스로 소리를
// 보내는가. "버스 볼륨을 0으로 내렸을 때 그 버스에 있는 소리만 꺼지는가" 로 판정한다.
// 이건 자동 테스트로 대신할 수 없다 — miniaudio 공개 API 로는 "이 sound 가 어느
// group 에 붙어 있나" 를 조회할 수 없어서, 마지막 한 칸은 귀로 메워야 한다.
//
// 사용법: 아무 오브젝트에나 붙이고 재생한 뒤 숫자키를 누른다.
//   1 = Master   2 = Music   3 = SFX   4 = Voice   5 = UI   (각각 0 ↔ 1 토글)
//   0 = 전부 1.0 으로 되돌리기
// 누를 때마다 현재 볼륨이 Debug Log 에 찍히므로 로그와 귀를 맞춰 보면 된다.
//
// 판정:
//  · 소리를 내는 AudioPlayer 의 Bus 를 Music 으로 두고 2번을 누르면 **꺼져야** 한다.
//  · 같은 상태에서 3번(SFX)을 누르면 **안 꺼져야** 한다. 꺼지면 라우팅이 무시되고
//    전부 한 버스로 가고 있는 것이다.
//  · 1번(Master)은 어느 버스에 있든 항상 꺼야 한다 — 표준 버스는 전부 Master 하위다.
//    2/3번이 안 먹는데 1번만 먹으면 예전 동작(전부 Master 직결)이라는 뜻이다.
//
// 등록 경로가 프로젝트마다 다르다 — 이 샘플은 GameModule.cpp 에서 손으로 등록하지만,
// 사용자 프로젝트는 에디터 코드 생성기가 `JBRO_SCRIPT` 마커를 grep 해서 자동 등록한다.
// 그래서 `class` 가 아니라 **JBRO_SCRIPT 로 선언한다**(매크로 확장 결과가 class 라 여기서도
// 그대로 컴파일된다). `class` 로 두면 생성기가 못 찾아 사용자 프로젝트의 에디터 목록에
// 아예 안 뜬다 — 컴파일도 되고 에러도 없어서 원인을 찾기 어렵다.
JBRO_SCRIPT CAudioBusProbeScript final
	: public CGameScript
	, public InputHandler<"Game">
{
	SCRIPT_CLASS(CAudioBusProbeScript)

protected:
	void OnStart() override;
	bool HandleInput(const InputDeviceContext& ctx) override;

private:
	// 버스 하나를 0 ↔ 1 로 뒤집고 결과를 로그로 남긴다. 버스를 못 얻으면 그 사실을 남긴다
	// (조용히 아무 일도 안 일어나는 게 제일 헷갈린다).
	void ToggleBus(EAudioBusKind kind);
	void ResetAllBuses();
	void LogAllBuses(const char* reason);
};
