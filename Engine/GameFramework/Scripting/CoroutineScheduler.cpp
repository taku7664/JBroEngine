#include "pch.h"
#include "CoroutineScheduler.h"

#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Scripting/GameScript.h"

#include <algorithm>

CCoroutineScheduler::~CCoroutineScheduler()
{
	// 정상 경로에선 DestroyScriptInstances 가 이미 CancelAll 했겠지만, 방어적으로 남은 프레임을 파괴한다.
	CancelAll();
}

CoroutineId CCoroutineScheduler::Start(Coroutine&& routine, const SafePtr<CGameScript>& owner)
{
	CGameScript* ownerRaw = owner.TryGet();
	Coroutine::handle_type handle = routine.Release();
	if (nullptr == ownerRaw || false == static_cast<bool>(handle))
	{
		// owner 가 없거나 빈 코루틴(이미 파괴/무효) — 시작하지 않는다. handle 이 있으면 파괴.
		if (handle)
		{
			DestroyChain(handle);
		}
		return CoroutineId{};
	}

	const CoroutineId id{ m_nextId++ };
	if (0 == m_nextId)
	{
		m_nextId = 1;   // 0 은 무효 id 예약 — 랩어라운드 시 건너뛴다.
	}

	Entry entry;
	entry.Id = id;
	entry.Owner = owner;
	entry.Handle = handle;

	// 순회 중(resume 안에서 호출)이면 지연 큐로 받아 m_entries 재할당으로 인한 반복자 무효화를 피한다.
	if (m_ticking)
	{
		m_pendingStarts.push_back(std::move(entry));
	}
	else
	{
		m_entries.push_back(std::move(entry));
	}
	return id;
}

void CCoroutineScheduler::Stop(CoroutineId id)
{
	if (false == id.IsValid())
	{
		return;
	}
	// m_entries 와 m_pendingStarts(틱 중 시작돼 아직 합류 전) 둘 다 본다 —
	// 시작 직후 같은 틱에 중지하는 경우를 놓치지 않게.
	for (Entry& entry : m_entries)
	{
		if (entry.Id.Value == id.Value)
		{
			entry.PendingRemoval = true;
			break;
		}
	}
	for (Entry& entry : m_pendingStarts)
	{
		if (entry.Id.Value == id.Value)
		{
			entry.PendingRemoval = true;
			break;
		}
	}
	if (false == m_ticking)
	{
		Sweep();
	}
}

void CCoroutineScheduler::StopForOwner(const CGameScript* owner)
{
	if (nullptr == owner)
	{
		return;
	}
	// owner 식별은 SafePtr 의 원시 포인터로 비교한다 — 이 시점에 owner 가 파괴 중이라
	// 컨트롤블록이 무효화됐어도 주소 동일성은 유효하다.
	for (Entry& entry : m_entries)
	{
		if (entry.Owner.TryGet() == owner)
		{
			entry.PendingRemoval = true;
		}
	}
	for (Entry& entry : m_pendingStarts)
	{
		if (entry.Owner.TryGet() == owner)
		{
			entry.PendingRemoval = true;
		}
	}
	if (false == m_ticking)
	{
		Sweep();
	}
}

void CCoroutineScheduler::CancelAll()
{
	for (Entry& entry : m_entries)
	{
		if (entry.Handle)
		{
			DestroyChain(entry.Handle);
			entry.Handle = {};
		}
	}
	m_entries.clear();

	for (Entry& entry : m_pendingStarts)
	{
		if (entry.Handle)
		{
			DestroyChain(entry.Handle);
			entry.Handle = {};
		}
	}
	m_pendingStarts.clear();
}

void CCoroutineScheduler::Tick(float deltaSeconds, float unscaledDeltaSeconds)
{
	RunStep(deltaSeconds, unscaledDeltaSeconds, TickContext::Update);
}

void CCoroutineScheduler::NotifyFixedUpdate()
{
	// 고정 스텝엔 dt 개념이 다르다 — 시간/프레임 대기는 여기서 소비하지 않고(문맥=Fixed),
	// WaitFixedUpdate 대기만 풀린다.
	RunStep(0.0f, 0.0f, TickContext::Fixed);
}

void CCoroutineScheduler::RunStep(float deltaSeconds, float unscaledDeltaSeconds, TickContext context)
{
	m_ticking = true;
	// 순회 중 시작된 코루틴은 지연 큐로 가므로, 이번 스텝의 대상은 시작 시점의 개수만.
	const std::size_t count = m_entries.size();
	for (std::size_t i = 0; i < count; ++i)
	{
		Entry& entry = m_entries[i];
		if (entry.PendingRemoval)
		{
			continue;
		}

		CGameScript* owner = entry.Owner.TryGet();
		if (nullptr == owner)
		{
			entry.PendingRemoval = true;   // owner 파괴 → 취소. 프레임 파괴는 sweep 에서.
			continue;
		}

		// 비활성 스크립트/오브젝트는 pause(취소 아님) — 다시 활성화되면 이어서 재개된다.
		CGameObject* ownerObject = owner->GetOwner().TryGet();
		const bool active = owner->IsEnabled() && nullptr != ownerObject && ownerObject->IsActiveInHierarchy();
		if (false == active)
		{
			continue;
		}

		if (false == AdvanceEntry(entry, deltaSeconds, unscaledDeltaSeconds, context))
		{
			entry.PendingRemoval = true;   // 최상위 완료.
		}
	}
	m_ticking = false;

	Sweep();

	// 지연 시작 코루틴을 본 목록으로 합류시킨다(다음 스텝부터 순회 대상). 단 순회 중 이미
	// 중지·취소로 마킹된 것은 합류시키지 않고 프레임을 파괴한다(시작 직후 같은 틱 중지 처리).
	if (false == m_pendingStarts.empty())
	{
		for (Entry& started : m_pendingStarts)
		{
			if (started.PendingRemoval)
			{
				if (started.Handle)
				{
					DestroyChain(started.Handle);
					started.Handle = {};
				}
				continue;
			}
			m_entries.push_back(std::move(started));
		}
		m_pendingStarts.clear();
	}
}

bool CCoroutineScheduler::AdvanceEntry(Entry& entry, float deltaSeconds, float unscaledDeltaSeconds, TickContext context)
{
	// 한 스텝에서 None/중첩-시작 같은 즉시 진행 상태는 이어서 돌리되, 실제 대기를 만나면 멈춘다.
	// guard 는 유저 코드가 co_await 없이 무한 중첩 시작을 만드는 병리적 경우의 안전장치.
	for (int guard = 0; guard < 1024; ++guard)
	{
		if (entry.PendingRemoval)
		{
			return true;   // resume 재진입에서 취소됨 — 더 건드리지 않는다(파괴는 sweep).
		}

		Coroutine::handle_type handle = DescendPop(entry.Handle);
		Coroutine::promise_type& promise = handle.promise();

		// None(갓 시작·중첩 완료 직후)은 Update 문맥에서만 즉시 재개한다. 그 외 대기는 조건 판정.
		const bool resumeNow = (CoroutineWaitKind::None == promise.Kind)
			? (TickContext::Update == context)
			: IsWaitReady(promise, deltaSeconds, unscaledDeltaSeconds, context);
		if (false == resumeNow)
		{
			return true;   // 아직 대기 중.
		}

		handle.resume();

		if (entry.PendingRemoval)
		{
			return true;   // resume 안에서 자기 자신(owner) 취소됨.
		}
		if (handle.done())
		{
			if (handle == entry.Handle)
			{
				return false;   // 최상위 완료 → 엔트리 종료.
			}
			continue;           // 중첩 자식 완료 → 다음 iter 의 DescendPop 이 pop 한다.
		}

		// 재개 후 다시 suspend. 새 대기가 실제 대기면 이번 스텝은 여기까지, None(중첩 자식 시작 등)이면 계속.
		Coroutine::handle_type next = DescendPop(entry.Handle);
		if (CoroutineWaitKind::None != next.promise().Kind)
		{
			return true;
		}
	}
	return true;
}

Coroutine::handle_type CCoroutineScheduler::DescendPop(Coroutine::handle_type top)
{
	Coroutine::handle_type handle = top;
	for (;;)
	{
		Coroutine::promise_type& promise = handle.promise();
		if (CoroutineWaitKind::Nested == promise.Kind)
		{
			if (false == static_cast<bool>(promise.Nested) || promise.Nested.done())
			{
				// 자식이 끝났거나 없음 → 자식 파괴 후 부모를 준비(None) 상태로.
				if (promise.Nested)
				{
					promise.Nested.destroy();
				}
				promise.Nested = {};
				promise.Kind = CoroutineWaitKind::None;
				return handle;   // 부모가 현재 핸들.
			}
			handle = promise.Nested;   // 살아있는 자식으로 하강.
			continue;
		}
		return handle;
	}
}

bool CCoroutineScheduler::IsWaitReady(Coroutine::promise_type& promise, float deltaSeconds, float unscaledDeltaSeconds, TickContext context)
{
	switch (promise.Kind)
	{
	case CoroutineWaitKind::None:
		return true;
	case CoroutineWaitKind::Seconds:
		if (TickContext::Update != context)
		{
			return false;
		}
		promise.WaitTime -= deltaSeconds;
		return promise.WaitTime <= 0.0f;
	case CoroutineWaitKind::SecondsRealtime:
		if (TickContext::Update != context)
		{
			return false;
		}
		promise.WaitTime -= unscaledDeltaSeconds;
		return promise.WaitTime <= 0.0f;
	case CoroutineWaitKind::Frames:
		if (TickContext::Update != context)
		{
			return false;
		}
		promise.FramesRemaining -= 1;
		return promise.FramesRemaining <= 0;
	case CoroutineWaitKind::FixedUpdate:
		return TickContext::Fixed == context;
	case CoroutineWaitKind::Until:
		if (TickContext::Update != context)
		{
			return false;
		}
		// 타입 소거 호출 — Invoke 는 게임 DLL 이 만든 정적 호출자, Context 는 코루틴 프레임 안의
		// 술어 객체다. 호스트는 술어 타입을 모른 채 호출만 한다(캡처 유무와 무관).
		return (nullptr != promise.PredicateInvoke) ? promise.PredicateInvoke(promise.PredicateContext) : true;
	case CoroutineWaitKind::Nested:
	default:
		return false;   // Nested 는 DescendPop 이 처리하므로 여기 도달하지 않는다.
	}
}

void CCoroutineScheduler::DestroyChain(Coroutine::handle_type top)
{
	Coroutine::handle_type handle = top;
	while (handle)
	{
		// 파괴 전에 다음(중첩 자식) 핸들을 먼저 확보한다 — destroy 후 promise 접근은 불가.
		Coroutine::handle_type next = (CoroutineWaitKind::Nested == handle.promise().Kind)
			? handle.promise().Nested
			: Coroutine::handle_type{};
		handle.destroy();
		handle = next;
	}
}

void CCoroutineScheduler::Sweep()
{
	for (Entry& entry : m_entries)
	{
		if (entry.PendingRemoval && entry.Handle)
		{
			DestroyChain(entry.Handle);
			entry.Handle = {};
		}
	}
	m_entries.erase(
		std::remove_if(m_entries.begin(), m_entries.end(),
			[](const Entry& entry) { return entry.PendingRemoval; }),
		m_entries.end());
}
