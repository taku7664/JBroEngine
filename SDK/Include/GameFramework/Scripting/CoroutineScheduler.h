#pragma once

#include "GameFramework/Scripting/Coroutine.h"
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <vector>

class CGameScript;

// ─────────────────────────────────────────────────────────────────────────────
//  CCoroutineScheduler — 캔버스가 소유하는 코루틴 스케줄러.
//
//  · 시작된 코루틴을 owner 스크립트의 SafePtr 과 함께 보관한다. owner 가 파괴되면 자동 취소.
//  · Tick(Update)·NotifyFixedUpdate(고정 스텝) 이 대기 조건이 충족된 코루틴을 재개한다.
//  · 재개(resume)는 스크립트 유저 코드를 실행하므로, 그 안에서 StartCoroutine/Stop 이 다시
//    불릴 수 있다(재진입). Tick 중에는 시작을 지연 큐로 받고, 취소는 프레임 파괴를 미뤄
//    (마킹만) 순회 후 sweep 에서 정리한다 — 실행 중인 프레임을 파괴하는 크래시를 막는다.
//  · CancelAll 은 시뮬 정지·DLL 라이브 리컴파일 관문(CGameCanvas::DestroyScriptInstances)에서
//    호출해, 살아있는 프레임을 소유 DLL 언로드 전에 전부 파괴한다.
// ─────────────────────────────────────────────────────────────────────────────
class CCoroutineScheduler
{
public:
	CCoroutineScheduler() = default;
	~CCoroutineScheduler();

	CCoroutineScheduler(const CCoroutineScheduler&) = delete;
	CCoroutineScheduler& operator=(const CCoroutineScheduler&) = delete;
	CCoroutineScheduler(CCoroutineScheduler&&) = delete;
	CCoroutineScheduler& operator=(CCoroutineScheduler&&) = delete;

	// 코루틴을 시작한다. owner 는 수명·활성 판정에 쓰인다(파괴 시 자동 취소, 비활성 시 pause).
	// 반환 id 로 나중에 Stop 할 수 있다. routine 은 소유가 스케줄러로 이전된다(호출 후 빈 상태).
	CoroutineId Start(Coroutine&& routine, const SafePtr<CGameScript>& owner);

	// 특정 코루틴/특정 owner 의 전체/모든 코루틴을 취소한다.
	void Stop(CoroutineId id);
	void StopForOwner(const CGameScript* owner);

	// 살아있는 모든 코루틴 프레임을 즉시 파괴한다(시뮬 정지·DLL 리로드·캔버스 클리어 관문).
	void CancelAll();

	// 프레임당 1회(스크립트 Update 직후). 스케일/언스케일 dt 를 받아 시간·프레임 대기를 소비한다.
	void Tick(float deltaSeconds, float unscaledDeltaSeconds);
	// 고정 스텝당(프레임당 0~N회). WaitFixedUpdate 로 멈춘 코루틴을 재개한다.
	void NotifyFixedUpdate();

	bool IsEmpty() const noexcept { return m_entries.empty(); }

private:
	struct Entry
	{
		CoroutineId          Id;
		SafePtr<CGameScript> Owner;
		Coroutine::handle_type Handle{};   // 최상위 핸들(중첩 자식은 promise.Nested 로 체인).
		bool                 PendingRemoval = false;
	};

	// tick 문맥 — 시간/프레임 대기는 Update 에서만 소비하고, FixedUpdate 대기는 Fixed 에서만 푼다.
	enum class TickContext { Update, Fixed };

	// 한 엔트리를 한 스텝 전진시킨다. false = 최상위까지 완료(제거 대상). true = 아직 살아있음.
	bool AdvanceEntry(Entry& entry, float deltaSeconds, float unscaledDeltaSeconds, TickContext context);
	// 중첩 체인에서 아직 안 끝난 가장 깊은 핸들로 내려가며, 완료된 자식은 파괴하고 부모를 준비 상태로.
	static Coroutine::handle_type DescendPop(Coroutine::handle_type top);
	// 현재 핸들의 대기 조건이 이 문맥에서 충족됐는가(시간/프레임 대기는 여기서 소비된다).
	static bool IsWaitReady(Coroutine::promise_type& promise, float deltaSeconds, float unscaledDeltaSeconds, TickContext context);
	// 최상위 핸들부터 중첩 체인 전체를 파괴한다.
	static void DestroyChain(Coroutine::handle_type top);

	// 공용 재개 루프(Update tick 과 FixedUpdate notify 가 문맥만 바꿔 공유).
	void RunStep(float deltaSeconds, float unscaledDeltaSeconds, TickContext context);
	// PendingRemoval 로 마킹된 엔트리의 프레임을 파괴하고 목록에서 제거한다(순회 밖에서만 호출).
	void Sweep();

	std::vector<Entry> m_entries;
	std::vector<Entry> m_pendingStarts;   // tick 중 시작된 코루틴(순회 후 합류).
	std::uint64_t      m_nextId = 1;
	bool               m_ticking = false;  // 순회 중이면 시작 지연·취소는 마킹만.
};
