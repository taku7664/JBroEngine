#include <JBro/Types/Delegate.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

namespace
{
    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    JBro::Int32 g_freeCallCount = 0;

    JBro::Int32 AddAndCount(JBro::Int32 left, JBro::Int32 right)
    {
        ++g_freeCallCount;
        return left + right;
    }

    void AppendOne(JBro::Int32& target)
    {
        target += 1;
    }

    struct Counter
    {
        JBro::Int32 value = 0;

        JBro::Int32 Add(JBro::Int32 amount)
        {
            value += amount;
            return value;
        }

        JBro::Int32 Subtract(JBro::Int32 amount)
        {
            value -= amount;
            return value;
        }

        JBro::Int32 Peek() const
        {
            return value;
        }
    };

    // 기존 엔진이 적어 온 콜백 모양이다(`AudioMixerDesc::openStream` 과 같은 꼴). 델리게이트가 이 모양과
    // 그대로 오갈 수 있어야 기존 경계를 한 번에 바꾸지 않고 한 자리씩 옮길 수 있다.
    using LegacyCallback = JBro::Int32 (*)(void* user, JBro::Int32 amount);

    void* g_lastUser = nullptr;

    // 부를 때 넘어온 사용자 자료를 그대로 적어 둔다. 멤버 함수로 확인하면 빈 `user` 가 왔을 때
    // 널 `this` 로 들어가 그냥 터지고, 무엇이 틀렸는지 말해 주는 단언이 남지 않는다.
    JBro::Int32 RecordUser(void* user, JBro::Int32 amount)
    {
        g_lastUser = user;
        return amount;
    }

    JBro::Int32 LegacyAdd(void* user, JBro::Int32 amount)
    {
        Counter* counter = static_cast<Counter*>(user);
        counter->value += amount;
        return counter->value;
    }

    // **빈 델리게이트는 걸리지 않은 것이고, `Reset` 은 두 칸을 함께 비운다.**
    // 함수 포인터만 비우고 `user` 를 남기면 다음에 거는 쪽이 옛 사용자 자료를 물려받는다.
    void TestDefaultIsUnboundAndResetClearsBothFields()
    {
        JBro::Delegate<JBro::Int32(JBro::Int32)> handler;
        Check(false == handler.IsBound(), "a default delegate is not bound");
        Check(handler.function == nullptr, "its function is empty");
        Check(handler.user == nullptr, "and so is its user data");

        Counter counter;
        handler = JBro::Delegate<JBro::Int32(JBro::Int32)>::Bind<&Counter::Add>(&counter);
        Check(handler.IsBound(), "binding makes it callable");

        handler.Reset();
        Check(false == handler.IsBound(), "reset unbinds it");
        Check(handler.user == nullptr, "and reset clears the user pointer too, not just the function");
    }

    // **자유 함수는 사용자 자료 없이 걸린다.** 첫 인자 자리가 비어 있어도 호출이 성립해야 한다.
    void TestFreeFunctionBinding()
    {
        g_freeCallCount = 0;
        const auto handler = JBro::Delegate<JBro::Int32(JBro::Int32, JBro::Int32)>::Bind<&AddAndCount>();
        Check(handler.IsBound(), "a free function binds");
        Check(handler.user == nullptr, "and carries no user data");
        Check(handler.Invoke(2, 3) == 5, "the return value comes back");
        Check(g_freeCallCount == 1, "and the function really ran once");
    }

    // **멤버 함수는 인스턴스를 사용자 자료로 들고 간다.** 같은 함수라도 인스턴스가 다르면 다른 대상이다 -
    // 이것이 손으로 짝지을 때 가장 자주 어긋나던 자리다.
    void TestMemberFunctionBindingKeepsInstancesApart()
    {
        Counter first;
        Counter second;
        const auto toFirst = JBro::Delegate<JBro::Int32(JBro::Int32)>::Bind<&Counter::Add>(&first);
        const auto toSecond = JBro::Delegate<JBro::Int32(JBro::Int32)>::Bind<&Counter::Add>(&second);

        Check(toFirst.user == &first, "the instance is carried as the user pointer");
        Check(toSecond.user == &second, "and each binding carries its own");
        Check(toFirst.Invoke(3) == 3, "the first instance accumulates on its own");
        Check(toFirst.Invoke(4) == 7, "and keeps accumulating");
        Check(toSecond.Invoke(10) == 10, "the second instance is untouched by the first");
        Check(first.value == 7 && second.value == 10, "so the two never share state");

        Check(toFirst.function == toSecond.function, "the same member function produces the same thunk");
        Check(toFirst != toSecond, "but a different instance makes a different delegate");
    }

    // **const 멤버 함수도 걸린다.** 읽기만 하는 통지(조회 콜백)가 흔하다.
    void TestConstMemberFunctionBinding()
    {
        Counter counter;
        counter.value = 42;
        const auto handler = JBro::Delegate<JBro::Int32()>::Bind<&Counter::Peek>(&counter);
        Check(handler.Invoke() == 42, "a const member function binds and reads through");
    }

    // **참조 인자는 복사되지 않고 그대로 건너간다.** 출력 인자를 쓰는 콜백(`AudioFileDecoder&`)이 있어서
    // 값으로 잘려 나가면 호출한 쪽이 결과를 못 받는다.
    void TestReferenceArgumentsPassThrough()
    {
        const auto handler = JBro::Delegate<void(JBro::Int32&)>::Bind<&AppendOne>();
        JBro::Int32 target = 5;
        handler.Invoke(target);
        handler.Invoke(target);
        Check(target == 7, "the callee wrote through the reference, so it was not copied");
    }

    // **기존 `void (*)(void* user, ...)` 짝과 그대로 오간다.** 한 자리씩 옮기려면 양방향이어야 한다.
    void TestInteropWithRawThunkPairs()
    {
        Counter counter;
        const auto handler = JBro::Delegate<JBro::Int32(JBro::Int32)>::FromThunk(&LegacyAdd, &counter);
        Check(handler.function == &LegacyAdd, "the raw thunk is kept as given");
        Check(handler.user == &counter, "and so is its user pointer");
        Check(handler.Invoke(5) == 5, "a raw pair calls through");

        // 반대 방향: 델리게이트가 든 두 칸을 기존 API 에 그대로 건넨다.
        const LegacyCallback legacy = handler.function;
        void* legacyUser = handler.user;
        Check(legacy(legacyUser, 6) == 11, "and its two fields feed a legacy callback signature unchanged");
        Check(counter.value == 11, "both paths reached the same object");
    }

    // **부를 때 그 델리게이트의 사용자 자료가 건너간다.** 다른 것을 건네면 콜백은 엉뚱한 객체를 만진다 -
    // 손으로 두 변수를 짝지을 때 가장 조용히 틀리던 자리다.
    void TestInvokePassesItsOwnUserPointer()
    {
        Counter first;
        Counter second;

        g_lastUser = nullptr;
        const auto toFirst = JBro::Delegate<JBro::Int32(JBro::Int32)>::FromThunk(&RecordUser, &first);
        Check(toFirst.Invoke(1) == 1, "the call goes through");
        Check(g_lastUser == &first, "and it carried the pointer this delegate holds");

        g_lastUser = nullptr;
        const auto toSecond = JBro::Delegate<JBro::Int32(JBro::Int32)>::FromThunk(&RecordUser, &second);
        Check(toSecond.Invoke(2) == 2, "the second call goes through too");
        Check(g_lastUser == &second, "and carried the other target, not the first one");

        g_lastUser = nullptr;
        const auto unbound = JBro::Delegate<JBro::Int32(JBro::Int32)>::FromThunk(&RecordUser, nullptr);
        Check(unbound.Invoke(3) == 3, "an empty user pointer is still a valid target");
        Check(g_lastUser == nullptr, "and it arrives as an empty pointer, not as someone else's");
    }

    // **경계를 넘을 수 있는 값이다.** 바이트로 복사해도 살아 있어야 DLL 경계를 넘는 구조체 칸에 둘 수 있다.
    void TestIsPlainDataAndSurvivesAByteCopy()
    {
        using Handler = JBro::Delegate<JBro::Int32(JBro::Int32)>;
        static_assert(std::is_trivially_copyable_v<Handler>, "a delegate must cross the DLL boundary by value");
        static_assert(std::is_standard_layout_v<Handler>, "and keep a predictable layout");
        static_assert(sizeof(Handler) == sizeof(void*) * 2, "it is exactly a function pointer and a user pointer");

        Counter counter;
        const Handler source = Handler::Bind<&Counter::Add>(&counter);

        unsigned char buffer[sizeof(Handler)] = {};
        std::memcpy(buffer, &source, sizeof(Handler));
        Handler copied;
        std::memcpy(&copied, buffer, sizeof(Handler));

        Check(copied == source, "a byte copy is the same delegate");
        Check(copied.Invoke(9) == 9, "and it still calls through after the round trip");
    }

    // **같은 함수에 같은 대상이면 같다.** 두 번 거는 것을 막거나 뗄 대상을 찾을 때 이 비교에 기댄다.
    void TestEquality()
    {
        Counter counter;
        const auto left = JBro::Delegate<JBro::Int32(JBro::Int32)>::Bind<&Counter::Add>(&counter);
        const auto right = JBro::Delegate<JBro::Int32(JBro::Int32)>::Bind<&Counter::Add>(&counter);
        Check(left == right, "the same function and the same target compare equal");

        const auto subtract = JBro::Delegate<JBro::Int32(JBro::Int32)>::Bind<&Counter::Subtract>(&counter);
        Check(left.user == subtract.user, "the same target is carried by both");
        Check(left != subtract, "but a different member function makes a different delegate");

        const JBro::Delegate<JBro::Int32(JBro::Int32)> empty;
        Check(empty == JBro::Delegate<JBro::Int32(JBro::Int32)>(), "two unbound delegates are equal");
        Check(left != empty, "and a bound one never equals an unbound one");
    }
}

JBro::Int32 RunDelegateTests()
{
    try
    {
        TestDefaultIsUnboundAndResetClearsBothFields();
        TestFreeFunctionBinding();
        TestInvokePassesItsOwnUserPointer();
        TestMemberFunctionBindingKeepsInstancesApart();
        TestConstMemberFunctionBinding();
        TestReferenceArgumentsPassThrough();
        TestInteropWithRawThunkPairs();
        TestIsPlainDataAndSurvivesAByteCopy();
        TestEquality();
    }
    catch (const std::exception&)
    {
        return 1;
    }

    std::cout << "delegate tests passed\n";
    return 0;
}
