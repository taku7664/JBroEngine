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
// 사용법: 아무 오브젝트에나 붙이고 재생한 뒤 숫자키를 누른다. 숫자키는 **프로젝트 세팅의
// 버스 목록 순서**에 매핑된다(1 = Master, 2부터는 세팅에 적은 순서). OnStart 로그가
// 어느 키가 어느 버스인지 찍어 주므로 그걸 보고 누르면 된다.
//   1..9 = 해당 버스 볼륨 0 ↔ 1 토글
//   0    = 전부 1.0 으로 되돌리기
//
// 판정:
//  · 소리를 내는 AudioPlayer 의 Bus 를 "Music" 으로 두고 Music 키를 누르면 **꺼져야** 한다.
//  · 같은 상태에서 다른 버스(예: SFX) 키를 누르면 **안 꺼져야** 한다. 꺼지면 라우팅이
//    무시되고 전부 한 버스로 가고 있는 것이다.
//  · Master 키는 어느 버스에 있든 항상 꺼야 한다 — 모든 버스가 Master 하위다.
//    다른 버스는 안 먹는데 Master 만 먹으면 예전 동작(전부 Master 직결)이라는 뜻이다.
//  · 로그의 `(bus=...)` 가 누른 이름과 다르면 그 이름이 프로젝트 세팅에 없어서
//    Master 로 폴백된 것이다.
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
	void ToggleBus(const std::string& name);
	void ResetAllBuses();
	void LogAllBuses(const char* reason);

	// 숫자키 1..9 까지만 매핑한다 — 그 이상은 키가 모자라고, 진단에는 이 정도면 충분하다.
	static constexpr std::size_t kMaxProbeKeys = 9;

	// OnStart 에서 디바이스로부터 받아 둔 버스 목록(= 프로젝트 세팅 순서).
	std::vector<std::string> m_busNames;
};
