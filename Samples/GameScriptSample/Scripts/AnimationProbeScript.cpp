#include "pch.h"
#include "Scripts/AnimationProbeScript.h"

#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Component/SpriteAnimator2D.h"
#include "GameFramework/Component/SpriteRenderer2D.h"

#include <cstdio>
#include <string>

namespace
{
	// make_anim_assets.py 가 만든 자산의 고정 guid.
	constexpr const char* SHEET_GUID = "aa11000000000000000000000000ee01";
	constexpr const char* LOOP_GUID  = "aa11000000000000000000000000ee02";
	constexpr const char* ONCE_GUID  = "aa11000000000000000000000000ee03";

	void ProbeLog(const std::string& message)
	{
		if (Script.Debug)
		{
			Script.Debug->Log("[AnimProbe] " + message);
		}
	}
}

void CAnimationProbeScript::Check(bool condition, const char* label)
{
	if (condition)
	{
		++m_passCount;
		ProbeLog(std::string("PASS  ") + label);
		return;
	}
	++m_failCount;
	ProbeLog(std::string("FAIL  ") + label);
}

void CAnimationProbeScript::CheckText(const char* actual, const char* expected, const char* label)
{
	const std::string actualText   = nullptr != actual ? actual : "<null>";
	const std::string expectedText = nullptr != expected ? expected : "<null>";
	if (actualText == expectedText)
	{
		++m_passCount;
		ProbeLog(std::string("PASS  ") + label + "   \"" + actualText + "\"");
		return;
	}
	++m_failCount;
	ProbeLog(std::string("FAIL  ") + label + "   expected \"" + expectedText + "\", got \"" + actualText + "\"");
}

void CAnimationProbeScript::CheckCount(int actual, int expected, const char* label)
{
	if (actual == expected)
	{
		++m_passCount;
		ProbeLog(std::string("PASS  ") + label + "   " + std::to_string(actual));
		return;
	}
	++m_failCount;
	ProbeLog(std::string("FAIL  ") + label + "   expected " + std::to_string(expected)
		+ ", got " + std::to_string(actual));
}

void CAnimationProbeScript::OnAnimationEvent(const char* clipName, const char* eventName)
{
	m_lastEventClip = nullptr != clipName ? clipName : "";
	const std::string name = nullptr != eventName ? eventName : "";
	if ("loopTick1" == name) ++m_loopTick1Count;
	else if ("loopTick3" == name) ++m_loopTick3Count;
	else if ("onceDone" == name) ++m_onceDoneCount;
}

void CAnimationProbeScript::OnAnimationEnd(const char* clipName)
{
	++m_endCount;
	m_lastEndClip = nullptr != clipName ? clipName : "";
}

void CAnimationProbeScript::OnStart()
{
	if (m_started)
	{
		return;
	}
	m_started = true;
	StartCoroutine(RunProbe());
}

Coroutine CAnimationProbeScript::RunProbe()
{
	ProbeLog("START — attaching animator to this object");

	SafePtr<CGameCanvas> canvas = GetCanvas();
	CGameObject* owner = GetOwner().TryGet();
	if (nullptr == owner)
	{
		ProbeLog("ABORT — no owner");
		co_return;
	}

	// 애니메이터는 **자기 오브젝트**에 붙인다 — 그래야 이벤트 훅이 여기로 온다.
	SpriteRenderer2D* renderer = canvas->AddComponent<SpriteRenderer2D>(*owner);
	renderer->SpriteGuid = AssetGuid(std::string(SHEET_GUID));

	SpriteAnimator2D* animator = canvas->AddComponent<SpriteAnimator2D>(*owner);
	animator->ClipGuids.Add(AssetGuid(std::string(LOOP_GUID)));
	animator->ClipGuids.Add(AssetGuid(std::string(ONCE_GUID)));
	animator->DefaultClip = "probeLoop";

	// 시트가 실제로 4프레임으로 슬라이스됐는지부터. 이게 0 이면 뒤가 전부 무의미하다.
	co_await Wait::Seconds(0.2f);
	ProbeLog("--- default clip ---");
	CheckText(animator->GetCurrentClip(), "probeLoop", "default clip selected by name");
	Check(animator->IsPlaying(), "default clip is playing");

	// 루프 클립 10fps × 4프레임 = 0.4초/바퀴. 1.3초면 3바퀴 남짓.
	co_await Wait::Seconds(1.3f);

	ProbeLog("--- loop clip events ---");
	Check(m_loopTick1Count >= 2, "frame-1 event fired on repeat");
	Check(m_loopTick3Count >= 2, "frame-3 event fired on repeat");
	CheckText(m_lastEventClip.c_str(), "probeLoop", "event carries its clip name");
	CheckCount(m_endCount, 0, "looping clip never reports end");
	Check(renderer->FrameIndex <= 3, "frame index stays inside the sheet");

	// ── 이름으로 전환 ────────────────────────────────────────────────────────
	ProbeLog("--- switch by name ---");
	const int tick1Before = m_loopTick1Count;
	animator->Play("probeOnce");
	co_await Wait::Seconds(0.1f);
	CheckText(animator->GetCurrentClip(), "probeOnce", "Play switches the current clip");

	// 3프레임 비루프 = 0.3초면 끝난다. 넉넉히 기다린다.
	co_await Wait::Seconds(0.8f);

	ProbeLog("--- non-looping clip end ---");
	CheckCount(m_onceDoneCount, 1, "last-frame event fired exactly once");
	CheckCount(m_endCount, 1, "end hook fired exactly once");
	CheckText(m_lastEndClip.c_str(), "probeOnce", "end hook carries its clip name");
	Check(false == animator->IsPlaying(), "clip stopped at its last frame");
	CheckCount(static_cast<int>(renderer->FrameIndex), 2, "stopped on the clip's last frame");
	CheckCount(m_loopTick1Count, tick1Before, "old clip stops firing after the switch");

	// ── 없는 이름은 무시 ─────────────────────────────────────────────────────
	ProbeLog("--- unknown clip name ---");
	animator->Play("noSuchClip");
	co_await Wait::Seconds(0.2f);
	CheckText(animator->GetCurrentClip(), "probeOnce", "unknown name leaves the current clip alone");
	CheckCount(m_endCount, 1, "unknown name does not restart or re-end");

	// ── 다시 재생 / 정지 ─────────────────────────────────────────────────────
	ProbeLog("--- replay and stop ---");
	animator->Play("probeLoop");
	co_await Wait::Seconds(0.5f);
	CheckText(animator->GetCurrentClip(), "probeLoop", "replay switches back");
	Check(animator->IsPlaying(), "replay resumes playing");

	animator->Stop();
	co_await Wait::Seconds(0.1f);
	Check(false == animator->IsPlaying(), "Stop halts playback");
	const std::uint32_t frozenFrame = renderer->FrameIndex;
	co_await Wait::Seconds(0.4f);
	CheckCount(static_cast<int>(renderer->FrameIndex), static_cast<int>(frozenFrame), "frame frozen while stopped");

	const int total = m_passCount + m_failCount;
	if (0 == m_failCount)
	{
		ProbeLog("RESULT " + std::to_string(m_passCount) + "/" + std::to_string(total) + " PASS");
	}
	else
	{
		ProbeLog("RESULT " + std::to_string(m_passCount) + "/" + std::to_string(total)
			+ " PASS  (" + std::to_string(m_failCount) + " FAIL)");
	}

	ProbeLog("END");
	co_return;
}
