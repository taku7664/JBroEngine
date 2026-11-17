#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

#include <Windows.h>

namespace JBro::CompilerOutput
{
    // 분석·생성은 구문 트리를 재귀로 돈다. 식은 `MaxExpressionDepth`(D-374)로 막지만 문장 블록의 겹은 막지 않으므로,
    // 일은 스택을 크게 예약한 스레드에서 하고 그래도 넘치면 결함으로 알린다(jbroc-rules §15). `jbroc` 진입점이 쓰고,
    // 시험(`ScriptCompilerCommandLine`)은 같은 함수를 작은 스택으로 불러 넘침을 받는 길을 구성과 무관하게 지나간다.
    using GuardedBody = Int32 (*)(void* user);

    struct GuardedRun
    {
        Int32 Result = 0;
        Bool StackOverflow = false;
    };

    namespace StackGuardDetail
    {
        struct Work
        {
            GuardedBody Body;
            void* User;
            GuardedRun Run;
        };

        // `__try` 를 쓰는 함수에는 해제할 C++ 객체를 두지 않는다(C2712).
        inline void CallGuardingStack(Work& work)
        {
            __try
            {
                work.Run.Result = work.Body(work.User);
            }
            __except (GetExceptionCode() == EXCEPTION_STACK_OVERFLOW ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
            {
                work.Run.StackOverflow = true;
            }
        }

        inline DWORD WINAPI RunWork(void* data)
        {
            CallGuardingStack(*static_cast<Work*>(data));
            return 0;
        }
    }

    // `body(user)` 를 스택 `stackReserve` 바이트를 예약한 스레드에서 부르고 끝날 때까지 기다린다.
    // 넘치면 `StackOverflow` 가 참이고 `Result` 는 `overflowResult` 다. 스레드를 못 만들면 부른 스레드에서 같은 보호로 부른다.
    inline GuardedRun RunOnLargeStack(GuardedBody body, void* user, SIZE_T stackReserve, Int32 overflowResult)
    {
        StackGuardDetail::Work work{body, user, GuardedRun{overflowResult, false}};
        const HANDLE thread = CreateThread(nullptr, stackReserve, &StackGuardDetail::RunWork, &work,
            STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
        if (thread == nullptr)
        {
            StackGuardDetail::CallGuardingStack(work);
        }
        else
        {
            WaitForSingleObject(thread, INFINITE);
            CloseHandle(thread);
        }
        if (work.Run.StackOverflow)
        {
            work.Run.Result = overflowResult;
        }
        return work.Run;
    }
}
