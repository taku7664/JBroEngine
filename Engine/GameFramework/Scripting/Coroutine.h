#pragma once

#include <coroutine>
#include <cstdint>

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
	Until,            // 술어가 true 를 반환할 때까지 대기(captureless).
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
	struct WaitUntil { bool (*Predicate)() = nullptr; };   // 캡처 없는 술어/자유함수만.

	struct promise_type;
	using handle_type = std::coroutine_handle<promise_type>;

	struct promise_type
	{
		// 현재 대기 상태(스케줄러가 읽고 갱신).
		CoroutineWaitKind Kind = CoroutineWaitKind::None;
		float             WaitTime = 0.0f;         // Seconds/SecondsRealtime 남은 시간.
		int               FramesRemaining = 0;     // Frames 남은 프레임(멤버명이 타입 WaitFrames 를 가리지 않게 구분).
		bool (*Predicate)() = nullptr;             // Until 술어.
		handle_type       Nested{};             // Nested — co_await 한 자식 코루틴.

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
		std::suspend_always await_transform(WaitUntil w) noexcept
		{
			Kind = CoroutineWaitKind::Until;
			Predicate = w.Predicate;
			return {};
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
	// 술어는 캡처가 없어야 한다(bool(*)()). 캡처 조건은 `while (!cond) co_await Wait::NextFrame();`.
	inline Coroutine::WaitUntil Until(bool (*predicate)()) noexcept { return { predicate }; }
}
