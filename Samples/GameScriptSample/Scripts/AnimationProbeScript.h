#pragma once

#include "GameFramework/Scripting/ScriptAPI.h"

// ── CAnimationProbeScript ─────────────────────────────────────────────────────
// 애니메이션 클립(.janimclip) 자가검증 스크립트.
//
// 애니메이터를 **자기 오브젝트에** 붙인다. 애니메이션 훅(OnAnimationEvent/OnAnimationEnd)은
// 애니메이터와 같은 오브젝트의 스크립트로 디스패치되므로, 별도 오브젝트를 만들면 이 스크립트가
// 이벤트를 받지 못한다.
//
// ── 필요한 자산 ──────────────────────────────────────────────────────────────
// 아래 세 파일이 프로젝트 Assets/ 아래에 있어야 한다. guid 는 .cpp 에 박혀 있으므로
// .jmeta 의 Guid 를 그대로 맞출 것.
//
//  1) AnimProbeSheet.png  — 64x16 PNG. 16x16 프레임 4개가 가로로 늘어선 시트
//                           (색만 다르면 되므로 내용은 아무래도 좋다).
//     .jmeta: Guid aa11000000000000000000000000ee01 / Type Sprite / Importer Sprite
//     ImportOptions: SliceType CellCount, RowCount 1, ColumnCount 4,
//                    CellWidth 16, CellHeight 16, 나머지 0, Pivot 0.5/0.5, PPU 0
//
//  2) AnimProbeLoop.janimclip — Guid aa11000000000000000000000000ee02 / Type AnimationClip
//       Clip: Sprite <시트 guid>, Name probeLoop, StartFrame 0, FrameCount 4,
//             FramesPerSecond 10, Loop true,
//             Events: [{Frame 1, Name loopTick1}, {Frame 3, Name loopTick3}]
//
//  3) AnimProbeOnce.janimclip — Guid aa11000000000000000000000000ee03 / Type AnimationClip
//       Clip: Sprite <시트 guid>, Name probeOnce, StartFrame 0, FrameCount 3,
//             FramesPerSecond 10, Loop false,
//             Events: [{Frame 2, Name onceDone}]
//
// 자산이 없으면 시트 프레임 수가 0 이라 클립이 선택되지 않고 첫 검사부터 FAIL 로 드러난다
// (조용히 통과하지 않는다).
//
// 시간 기반 검사는 프레임률 흔들림에 견디도록 "최소 몇 회" 식으로 느슨하게 두고,
// 이름/횟수/정지 상태처럼 결정적인 것만 정확히 본다.
JBRO_SCRIPT CAnimationProbeScript final : public CGameScript
{
	SCRIPT_CLASS(CAnimationProbeScript)

protected:
	void OnStart() override;
	void OnAnimationEvent(const char* clipName, const char* eventName) override;
	void OnAnimationEnd(const char* clipName) override;

private:
	Coroutine RunProbe();

	void Check(bool condition, const char* label);
	void CheckText(const char* actual, const char* expected, const char* label);
	void CheckCount(int actual, int expected, const char* label);

	int m_passCount = 0;
	int m_failCount = 0;

	// 훅 수신 기록.
	int         m_loopTick1Count = 0;
	int         m_loopTick3Count = 0;
	int         m_onceDoneCount  = 0;
	int         m_endCount       = 0;
	std::string m_lastEndClip;
	std::string m_lastEventClip;

	bool m_started = false;
};
