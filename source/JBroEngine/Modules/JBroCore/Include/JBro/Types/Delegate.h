#pragma once

#include <cstddef>

namespace JBro
{
    template<typename Signature>
    struct Delegate;

    // 함수 포인터 하나와 사용자 자료 포인터 하나를 **한 값으로 묶은** 호출 대상이다(D-246).
    //
    // 엔진은 DLL 경계와 스레드 경계를 넘는 콜백을 `void (*)(void* user, ...)` 와 그 짝인 `void* user` 로
    // 적어 왔다(`AudioMixerDesc::openStream`·`IPlatform::AudioRenderCallback`·`JAllocator::AllocateThunk`).
    // 모양 자체는 옳다 - POD 라서 경계를 넘고, 캡처를 소유하지 않아 프레임 경로에서 할당하지 않는다.
    // 다만 **둘이 별개의 변수라서 짝이 어긋나도 컴파일러가 잡지 못했다.** 한쪽만 대입하거나, 한쪽만
    // 원자적으로 바꾸거나, 부를 때 엉뚱한 `user` 를 건네는 실수가 전부 실행 중에야 드러났다.
    //
    // 이 타입은 그 쌍을 한 값으로 만든다. 그 이상은 하지 않는다 - 소유도, 할당도, 가상 호출도 없다.
    // `std::function` 을 대신하는 것이 아니라, **경계를 넘는 자리에서 `std::function` 을 쓰지 않아도 되게**
    // 하는 것이 목적이다. 캡처를 들고 다녀야 하면 이 타입이 아니라 그 상태를 가진 객체를 `user` 로 넘긴다.
    //
    // 크기는 포인터 둘이고 trivially copyable 이다. 구조체 칸에 그대로 두고 `memcpy` 로 경계를 넘길 수 있다.
    //
    // **수명은 이 타입이 책임지지 않는다.** `user` 가 가리키는 객체가 먼저 죽으면 매달린 포인터가 된다.
    // 거는 쪽이 객체보다 먼저 `Reset` 하거나, 객체가 자기 소멸자에서 떼야 한다.
    //
    // **원자적으로 바꿔야 하는 자리에서는 이 타입 하나로 충분하지 않다.** 16 바이트 값은 x64 에서 무잠금
    // 원자 교체가 보장되지 않는다. 오디오 버스 처리기(D-206)처럼 다른 스레드가 읽는 중에 갈아 끼워야 하면
    // 지금처럼 "떼고 · 들어간 표시가 내려가기를 기다리고 · 새로 거는" 차례가 여전히 필요하다.
    template<typename R, typename... Args>
    struct Delegate<R(Args...)>
    {
        // 경계를 넘는 실제 함수 모양이다. 첫 인자가 언제나 사용자 자료다.
        using Thunk = R (*)(void*, Args...);

        Thunk function = nullptr;
        void* user = nullptr;

        // ── 거는 법 ────────────────────────────────────────────────────────────────────────────

        // 멤버 함수를 건다. `instance` 가 `user` 로 간다.
        // 쓰임: `Delegate<bool(int)>::Bind<&EngineInstance::OnTick>(this)`
        template<auto Method, typename Class>
        static Delegate Bind(Class* instance) noexcept
        {
            Delegate result;
            result.function = [](void* user, Args... args) -> R
            {
                return (static_cast<Class*>(user)->*Method)(static_cast<Args&&>(args)...);
            };
            result.user = instance;
            return result;
        }

        // 자유 함수(또는 정적 멤버 함수)를 건다. `user` 는 비어 있다.
        // 쓰임: `Delegate<void(float)>::Bind<&WriteLog>()`
        template<auto Function>
        static Delegate Bind() noexcept
        {
            Delegate result;
            result.function = [](void*, Args... args) -> R
            {
                return Function(static_cast<Args&&>(args)...);
            };
            result.user = nullptr;
            return result;
        }

        // 이미 `void (*)(void* user, ...)` 모양으로 적힌 함수와 그 짝을 그대로 받는다.
        // 기존 콜백 타입과 잇거나, 캡처 없는 람다를 바로 건넬 때 쓴다.
        static Delegate FromThunk(Thunk thunk, void* userData) noexcept
        {
            Delegate result;
            result.function = thunk;
            result.user = userData;
            return result;
        }

        // ── 쓰는 법 ────────────────────────────────────────────────────────────────────────────

        bool IsBound() const noexcept
        {
            return function != nullptr;
        }

        // **건 것이 있을 때만 부른다.** 비어 있는지는 부르는 쪽이 `IsBound` 로 본다 - 반환값이 있는 델리게이트에서
        // "비었을 때 무엇을 돌려줄지" 를 이 타입이 임의로 정하지 않기 위해서다.
        R Invoke(Args... args) const
        {
            return function(user, static_cast<Args&&>(args)...);
        }

        void Reset() noexcept
        {
            function = nullptr;
            user = nullptr;
        }

        // 같은 함수에 같은 사용자 자료가 걸렸는지 본다. 두 번 거는 것을 막거나 떼어낼 대상을 찾을 때 쓴다.
        friend bool operator==(const Delegate& left, const Delegate& right) noexcept
        {
            return left.function == right.function && left.user == right.user;
        }

        friend bool operator!=(const Delegate& left, const Delegate& right) noexcept
        {
            return false == (left == right);
        }
    };
}
