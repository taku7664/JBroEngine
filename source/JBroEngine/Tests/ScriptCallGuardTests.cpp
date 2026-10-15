#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Runtime/ScriptCallGuard.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 스크립트 훅의 예외를 받는 `ScriptCallGuard`(D-330)와 그 자리를 읽는 플랫폼(`CaptureFaultFrames`·`DescribeCodeAddress`)을 본다.
// 진짜 스크립트 DLL 없이, 스크립트가 할 법한 잘못(null 에 쓰기·끝없는 재귀·C++ 예외)을 이 실행 파일 안의 함수로 낸다.

namespace
{
    using namespace JBro;

    void Check(Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    constexpr UInt32 AccessViolation = 0xC0000005u;
    constexpr UInt32 StackOverflow = 0xC00000FDu;
    constexpr UInt32 CppException = 0xE06D7363u;

    // null 에 쓰는 줄의 번호다. 주소를 소스 줄로 바꾼 결과와 맞춘다.
    int g_nullWriteLine = 0;

    __declspec(noinline) void WriteThroughNull()
    {
        volatile int* nothing = nullptr;
        g_nullWriteLine = __LINE__ + 1;
        *nothing = 1;
    }

#pragma warning(push)
#pragma warning(disable : 4717)
    __declspec(noinline) int RecurseForever(int depth)
    {
        volatile char pad[256];
        pad[0] = static_cast<char>(depth);
        return RecurseForever(depth + 1) + pad[0];
    }
#pragma warning(pop)

    __declspec(noinline) void ThrowFromScript()
    {
        throw std::runtime_error("thrown by a script");
    }

    UInt32 CaptureWith(void* user, const void* exceptionPointers, UInt64* frames, UInt32 capacity)
    {
        return static_cast<IPlatform*>(user)->CaptureFaultFrames(exceptionPointers, frames, capacity);
    }

    const char* FileNameOf(const String& path)
    {
        const char* name = path.c_str();
        for (const char* cursor = path.c_str(); *cursor != '\0'; ++cursor)
        {
            if (*cursor == '/' || *cursor == '\\')
            {
                name = cursor + 1;
            }
        }
        return name;
    }

    // 꺼져 있으면(게임) 그냥 부른다. 감싸지 않으므로 예외도 받지 않는다.
    void TestAnOffGuardJustCalls()
    {
        ScriptCallGuard::Disable();
        ScriptCallGuard::ClearFault();
        Int32 calls = 0;
        Check(ScriptCallGuard::Call("OnUpdate", [&calls] { ++calls; }), "an off guard reports the call as made");
        Check(calls == 1, "and makes it");
        Check(false == ScriptCallGuard::HasFault(), "with nothing written down");
    }

    // 잘못된 메모리 접근을 받아 코드·훅·자리를 적고, 그 자리는 이 파일의 그 줄로 읽힌다.
    void TestAnAccessViolationIsCaughtAndLocated()
    {
        WindowsPlatform platform;
        ScriptCallGuard::Enable(&CaptureWith, &platform);
        ScriptCallGuard::ClearFault();

        Check(false == ScriptCallGuard::Call("OnUpdate", [] { WriteThroughNull(); }), "a call that faults reports failure");
        Check(ScriptCallGuard::HasFault(), "and the fault is written down");
        const ScriptFault& fault = ScriptCallGuard::GetFault();
        Check(fault.code == AccessViolation, "as an access violation");
        Check(fault.hook != nullptr && std::strcmp(fault.hook, "OnUpdate") == 0, "in the hook that was running");
        Check(fault.frameCount > 2, "with the stack from the faulting place outward");
        Check(fault.frames[0] == fault.address, "whose first frame is the faulting instruction");

        CodeLocation location;
        Check(platform.DescribeCodeAddress(fault.frames[0], location), "the faulting address belongs to a module");
        Check(std::strstr(location.function.c_str(), "WriteThroughNull") != nullptr, "the function is named");
        Check(std::strcmp(FileNameOf(location.file), "ScriptCallGuardTests.cpp") == 0, "the source file is found");
        Check(location.line == static_cast<std::uint32_t>(g_nullWriteLine), "and the line is the one that wrote through null");

        // 바깥 프레임 하나는 이 시험 함수 안이어야 한다 - 거슬러 올라가기가 예외 자리를 넘어 부른 쪽까지 간다.
        Bool sawCaller = false;
        for (UInt32 index = 1; index < fault.frameCount; ++index)
        {
            CodeLocation outer;
            if (platform.DescribeCodeAddress(UInt64(fault.frames[index].Value - 1), outer)
                && std::strstr(outer.function.c_str(), "TestAnAccessViolationIsCaughtAndLocated") != nullptr)
            {
                sawCaller = true;
                break;
            }
        }
        Check(sawCaller, "the walk reaches the code that called the hook");

        ScriptCallGuard::ClearFault();
        ScriptCallGuard::Disable();
    }

    // 예외가 난 뒤의 훅은 부르지 않는다. 지우면 다시 부른다.
    void TestHooksAfterAFaultAreSkippedUntilCleared()
    {
        WindowsPlatform platform;
        ScriptCallGuard::Enable(&CaptureWith, &platform);
        ScriptCallGuard::ClearFault();

        ScriptCallGuard::Call("OnUpdate", [] { WriteThroughNull(); });
        Int32 calls = 0;
        Check(false == ScriptCallGuard::Call("OnFixedUpdate", [&calls] { ++calls; }), "a hook after a fault is refused");
        Check(calls == 0, "and not called - the canvas it would see can no longer be trusted");
        Check(std::strcmp(ScriptCallGuard::GetFault().hook, "OnUpdate") == 0, "the first fault is the one kept");

        ScriptCallGuard::ClearFault();
        Check(ScriptCallGuard::Call("OnFixedUpdate", [&calls] { ++calls; }), "once cleared, hooks run again");
        Check(calls == 1, "and are called");
        ScriptCallGuard::Disable();
    }

    // 끝없는 재귀를 받고, 지킴 페이지를 되살려 **두 번째도** 받는다. 되살리지 않으면 두 번째 넘침에서 프로세스가 끝난다.
    void TestAStackOverflowIsCaughtTwice()
    {
        WindowsPlatform platform;
        ScriptCallGuard::Enable(&CaptureWith, &platform);
        for (Int32 round = 0; round < 2; ++round)
        {
            ScriptCallGuard::ClearFault();
            Check(false == ScriptCallGuard::Call("OnUpdate", [] { RecurseForever(0); }), "endless recursion is caught");
            Check(ScriptCallGuard::GetFault().code == StackOverflow, "as a stack overflow");
            Check(ScriptCallGuard::GetFault().frameCount == 0, "without walking the stack, which has no room left");
            Check(ScriptCallGuard::GetFault().address != 0, "but with the faulting address");
        }
        ScriptCallGuard::ClearFault();
        ScriptCallGuard::Disable();
    }

    // 스크립트가 던진 C++ 예외도 받는다.
    void TestACppExceptionIsCaught()
    {
        WindowsPlatform platform;
        ScriptCallGuard::Enable(&CaptureWith, &platform);
        ScriptCallGuard::ClearFault();
        Check(false == ScriptCallGuard::Call("OnStart", [] { ThrowFromScript(); }), "a thrown exception is caught");
        Check(ScriptCallGuard::GetFault().code == CppException, "as a C++ exception");
        ScriptCallGuard::ClearFault();
        ScriptCallGuard::Disable();
    }
}

JBro::Int32 RunScriptCallGuardTests()
{
    TestAnOffGuardJustCalls();
    TestAnAccessViolationIsCaughtAndLocated();
    TestHooksAfterAFaultAreSkippedUntilCleared();
    TestAStackOverflowIsCaughtTwice();
    TestACppExceptionIsCaught();
    std::cout << "ScriptCallGuard tests passed.\n";
    return 0;
}
