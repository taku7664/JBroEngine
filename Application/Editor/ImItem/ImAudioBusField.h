#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Core/Audio/AudioTypes.h"   // AudioBusDef, AUDIO_MASTER_BUS_NAME

#include <string>
#include <vector>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  오디오 믹싱 버스 **이름** 을 다루는 저작 측 공용 조각.
//
//  버스는 GUID 가 아니라 이름으로 지목한다(그 결정의 근거와 반례는
//  tasks/audio-bus-guid.md). 이름이 곧 참조라서 오타 하나가 조용한 Master 폴백이 되고,
//  이름을 바꾸면 그 이름을 적어 둔 쪽이 조용히 끊어진다. 그래서 저작 측에서:
//    · AudioPlayer 의 Bus 는 자유 입력이 아니라 **목록에서 고르게** 하고(Combo),
//    · 프로젝트 세팅의 이름 편집은 위험한 상태를 **빨갛게** 보여 준다(Is*/Find*).
//
//  인스펙터(컴포넌트)와 프로젝트 세팅이 둘 다 쓰므로 여기 모아 둔다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
namespace AudioBusUI
{
	// 현재 프로젝트가 고를 수 있는 버스 이름 전체. 첫 항목은 항상 "Master" 다
	// (예약 루트라 프로젝트 목록에는 없지만 언제나 선택 가능하다).
	// 프로젝트가 없으면 "Master" 하나만 돌아온다.
	std::vector<std::string> GetSelectableBusNames();

	// 이름 자체가 잘못됐는지 — 빈 이름이거나 목록 내 중복.
	// 프로젝트 세팅의 버스 행을 빨갛게 칠하는 판정이다.
	// "Master" 는 **막지 않는다** — 그 행은 루트 버스의 시작 음량으로 쓰인다.
	bool IsBusNameInvalid(const std::string& name, const std::vector<AudioBusDef>& all);

	// 창을 연 시점(baseline)에는 있었는데 지금 목록에서 사라진 이름들.
	// 그 이름을 적어 둔 AudioPlayer 와 스크립트의 GetBus("...") 가 Master 로 떨어진다.
	std::vector<std::string> FindRemovedBusNames(
		const std::vector<AudioBusDef>& current,
		const std::vector<std::string>& baseline);

	// 이 행이 "이름을 바꾼" 행으로 보이는지.
	//
	// 버스에 ID 가 없으므로 "Music→BGM 리네임" 과 "Music 삭제 + BGM 추가" 는 원리적으로
	// 구분할 수 없다(그게 GUID 를 검토했던 이유다). 그래서 **사라진 이름이 하나라도 있을 때**
	// 를 리네임 신호로 삼고, baseline 에 없던 이름을 가진 행에만 칠한다.
	// → 순수 추가는 사라진 이름이 없으므로 빨개지지 않는다.
	//   순수 삭제는 칠할 행이 없으므로 FindRemovedBusNames 로 따로 안내한다.
	bool IsBusNameRenamed(
		const std::string& name,
		const std::vector<AudioBusDef>& current,
		const std::vector<std::string>& baseline);

	// 버스 선택 드롭다운. busName 이 바뀌면 true.
	// 목록에 없는 이름이 들어 있어도 **그 값을 유지한 채** 항목으로 보여 준다 —
	// 조용히 첫 항목으로 스냅하면 사용자 모르게 라우팅이 바뀐다.
	bool DrawBusCombo(const char* id, std::string& busName);
}

#endif
