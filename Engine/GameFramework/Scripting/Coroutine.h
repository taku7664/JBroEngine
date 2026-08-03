#pragma once

#include <coroutine>
#include <cstdint>
#include <type_traits>
#include <utility>

// ─────────────────────────────────────────────────────────────────────────────
//  Coroutine — 스크립트용 C++20 네이티브 코루틴 반환 타입.
//
//  사용법(스크립트):
//      Coroutine OpenDoorAfterDelay()
//      {
//          co_await Wait::Seconds(1.0f);
//          OpenDoor();
//          co_await Wait::NextFrame();
//          Log("opened");
//      }
//      // 시작:  StartCoroutine(OpenDoorAfterDelay());
//
//  설계:
//   · lazy start — initial_suspend 가 suspend_always 라, 생성 즉시 실행되지 않고
//     스케줄러(CCoroutineScheduler)가 첫 resume 을 준다. 시작 순서를 스케줄러가 쥔다.
//   · 대기 상태는 promise 에 POD 로 담는다. 스케줄러가 typed handle 로 promise 를 읽어
//     매 프레임 재개 여부를 판정한다(co_await 지점마다 await_transform 이 상태를 갱신).
//   · 코루틴 프레임 alloc/free 는 전부 컴파일된 모듈(게임 DLL) 측 램프 함수로 처리되므로
//     호스트가 resume/destroy 해도 크로스힙 문제가 없다. coroutine_handle 은 사실상 void*.
//   · move-only 소유 래퍼 — 비어있지 않은 handle 을 든 쪽이 파괴 책임을 진다. 시작 시
//     스케줄러가, co_await 중첩 시 부모 promise 가 소유를 넘겨받는다(원본 handle 은 null 화).
// ─────────────────────────────────────────────────────────────────────────────

// StartCoroutine 이 돌려주는 식별자. 0 = 유효하지 않음. StopCoroutine 에 그대로 넘긴다.
struct CoroutineId
{
	std::uint64_t Value = 0;
	bool IsValid() const noexcept { return 0 != Value; }
};

// 대기 명령 종류. promise 가 현재 어떤 조건으로 멈춰 있는지 나타낸다.
enum class CoroutineWaitKind : std::uint8_t
{
	None,             // 대기 없음 — 다음 tick 에 즉시 재개(시작 직후·중첩 완료 직후).
	Seconds,          // 스케일 적용 시간(초) 경과 대기.
	SecondsRealtime,  // 언스케일 시간(초) 경과 대기(timeScale 무시).
	Frames,           // N 프레임(Update tick) 경과 대기.
	FixedUpdate,      // 다음 고정 스텝(FixedUpdate) 대기.
	Until,            // 술어가 true 를 반환할 때까지 대기(캡처 람다 가능).
	Nested,           // 다른 Coroutine 이 끝날 때까지 대기(co_await Coroutine).
};

class Coroutine
{
public:
	// ── co_await 대상 POD 명령 ──────────────────────────────────────────────
	struct WaitSeconds { float Seconds = 0.0f; };
	struct WaitSecondsRealtime { float Seconds = 0.0f; };
	struct WaitFrames { int Frames = 1; };
	struct WaitFixedUpdate {};
	// 술어를 **값으로** 담는다(캡처 람다 포함). 이 객체는 co_await 식의 피연산자로 awaiter 에
	// 옮겨지고, awaiter 는 중단 구간 동안 코루틴 프레임 안에 산다 — 그래서 std::function 같은
	// 별도 힙 할당이 필요 없고 주소도 안정적이다.
	template<typename TPredicate>
	struct WaitUntil { TPredicate Predicate; };

	struct promise_type;
	using handle_type = std::coroutine_handle<promise_type>;

	struct promise_type
	{
		// 현재 대기 상태(스케줄러가 읽고 갱신).
		CoroutineWaitKind Kind = CoroutineWaitKind::None;
		float             WaitTime = 0.0f;         // Seconds/SecondsRealtime 남은 시간.
		int               FramesRemaining = 0;     // Frames 남은 프레임(멤버명이 타입 WaitFrames 를 가리지 않게 구분).
		// Until 술어 — 타입 소거된 POD 두 개(호스트↔DLL 경계 계약). Context 는 코루틴 프레임 안
		// awaiter 의 술어 객체를, Invoke 는 그 타입 전용 정적 호출자(게임 DLL 코드)를 가리킨다.
		// 스케줄러(호스트)는 타입을 모른 채 Invoke(Context) 만 호출한다.
		void*             PredicateContext = nullptr;
		bool            (*PredicateInvoke)(void*) = nullptr;
		handle_type       Nested{};             // Nested — co_await 한 자식 코루틴.

		// Wait::Until 전용 awaiter. 술어를 값으로 들고 있어 중단 구간 동안 프레임 안에서 살아 있다.
		template<typename TPredicate>
		struct UntilAwaiter
		{
			TPredicate    Predicate;
			promise_type* Owner = nullptr;

			bool await_ready() const noexcept { return false; }
			void await_suspend(handle_type) noexcept
			{
				Owner->Kind = CoroutineWaitKind::Until;
				Owner->PredicateContext = static_cast<void*>(&Predicate);
				Owner->PredicateInvoke = [](void* context) -> bool
				{
					return (*static_cast<TPredicate*>(context))();
				};
			}
			void await_resume() const noexcept {}
		};

		Coroutine get_return_object() noexcept { return Coroutine{ handle_type::from_promise(*this) }; }

		// lazy start + 종료 후 suspend 유지(스케줄러가 done() 관측 후 destroy).
		std::suspend_always initial_suspend() noexcept { return {}; }
		std::suspend_always final_suspend() noexcept { return {}; }
		void return_void() noexcept {}
		// 스크립트에서 던진 예외는 코루틴 밖으로 전파하지 않고 그 코루틴만 종료시킨다
		// (한 코루틴의 오류가 프레임 루프 전체를 죽이지 않게 격리). 이후 final_suspend →
		// done() 이 되어 스케줄러가 정리한다.
		void unhandled_exception() noexcept {}

		// co_await Wait::* → 대기 상태를 갱신하고 항상 suspend(스케줄러가 재개 시점을 정한다).
		std::suspend_always await_transform(WaitSeconds w) noexcept
		{
			Kind = CoroutineWaitKind::Seconds;
			WaitTime = w.Seconds;
			return {};
		}
		std::suspend_always await_transform(WaitSecondsRealtime w) noexcept
		{
			Kind = CoroutineWaitKind::SecondsRealtime;
			WaitTime = w.Seconds;
			return {};
		}
		std::suspend_always await_transform(WaitFrames w) noexcept
		{
			Kind = CoroutineWaitKind::Frames;
			FramesRemaining = (w.Frames > 0) ? w.Frames : 1;
			return {};
		}
		std::suspend_always await_transform(WaitFixedUpdate) noexcept
		{
			Kind = CoroutineWaitKind::FixedUpdate;
			return {};
		}
		// 여기서 Kind 를 세우지 않는다 — 술어를 담은 awaiter 가 프레임에 자리잡은 뒤
		// (await_suspend) 그 주소를 promise 에 기록해야 한다.
		template<typename TPredicate>
		UntilAwaiter<TPredicate> await_transform(WaitUntil<TPredicate> w) noexcept
		{
			return UntilAwaiter<TPredicate>{ std::move(w.Predicate), this };
		}
		// co_await OtherRoutine() — 자식 코루틴 소유를 넘겨받아 끝날 때까지 대기.
		std::suspend_always await_transform(Coroutine&& child) noexcept
		{
			Kind = CoroutineWaitKind::Nested;
			Nested = child.m_handle;
			child.m_handle = {};   // 소유 이전 — child 소멸자가 프레임을 파괴하지 않게 null 화.
			return {};
		}
	};

	Coroutine() noexcept = default;
	explicit Coroutine(handle_type handle) noexcept : m_handle(handle) {}

	// move-only — 비어있지 않은 handle 을 든 쪽만 파괴 책임을 진다.
	Coroutine(const Coroutine&) = delete;
	Coroutine& operator=(const Coroutine&) = delete;
	Coroutine(Coroutine&& other) noexcept : m_handle(other.m_handle) { other.m_handle = {}; }
	Coroutine& operator=(Coroutine&& other) noexcept
	{
		if (this != &other)
		{
			if (m_handle)
			{
				m_handle.destroy();
			}
			m_handle = other.m_handle;
			other.m_handle = {};
		}
		return *this;
	}
	~Coroutine()
	{
		if (m_handle)
		{
			m_handle.destroy();
		}
	}

	// 소유권을 호출자에게 넘긴다(스케줄러가 시작 시 사용). 이후 이 객체는 빈 상태.
	handle_type Release() noexcept
	{
		handle_type handle = m_handle;
		m_handle = {};
		return handle;
	}

	bool IsValid() const noexcept { return static_cast<bool>(m_handle); }

private:
	friend struct promise_type;
	handle_type m_handle{};
};

// 스크립트가 co_await 에 쓰는 대기 팩토리 — 가독성 좋은 이름으로 POD 명령을 만든다.
namespace Wait
{
	inline Coroutine::WaitSeconds Seconds(float seconds) noexcept { return { seconds }; }
	inline Coroutine::WaitSecondsRealtime SecondsRealtime(float seconds) noexcept { return { seconds }; }
	inline Coroutine::WaitFrames Frames(int frames) noexcept { return { frames }; }
	inline Coroutine::WaitFrames NextFrame() noexcept { return { 1 }; }
	inline Coroutine::WaitFixedUpdate FixedUpdate() noexcept { return {}; }
	// 술어는 아무 호출 가능 객체나 된다 — 자유함수, 캡처 없는 람다, **캡처 람다**(`[this]`, `[=]`)
	// 모두 가능하다. 술어는 코루틴 프레임 안에 값으로 복사되므로 힙 할당이 없다.
	// 주의: 캡처한 대상(예: this)이 대기 도중 파괴되면 술어가 죽은 객체를 만진다. 오브젝트를
	// 캡처할 땐 그 코루틴이 소유 스크립트와 함께 취소된다는 점에 기대거나(자동 취소),
	// SafePtr 를 캡처해 유효성을 확인할 것.
	template<typename TPredicate>
	Coroutine::WaitUntil<std::decay_t<TPredicate>> Until(TPredicate&& predicate)
	{
		return { std::forward<TPredicate>(predicate) };
	}
}
