#include <JBro/Runtime/ScriptCallGuard.h>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

#include <cstdint>
#include <cstring>

namespace JBro::ScriptCallGuard
{
    namespace
    {
        // 스택 넘침의 예외 코드다(`EXCEPTION_STACK_OVERFLOW`). 이 모듈은 Windows 헤더를 보지 않는다.
        constexpr UInt32 StackOverflowCode = 0xC00000FDu;

        struct State
        {
            Bool enabled = false;
            Bool faulted = false;
            UInt32 depth = 0;
            ScriptFaultCapture capture = nullptr;
            void* user = nullptr;
            ScriptFault fault;
        };

        State& GetState()
        {
            static State state;
            return state;
        }

        // `EXCEPTION_POINTERS` 와 `EXCEPTION_RECORD` 의 앞부분이다. 코드와 주소만 읽는다 - 둘 다 Windows 의 ABI 라 바뀌지 않는다.
        struct ExceptionRecordHead
        {
            std::uint32_t code;
            std::uint32_t flags;
            void* chained;
            void* address;
        };

        struct ExceptionPointersHead
        {
            const ExceptionRecordHead* record;
            const void* context;
        };
    }

    void Enable(ScriptFaultCapture capture, void* user)
    {
        State& state = GetState();
        state.enabled = true;
        state.capture = capture;
        state.user = user;
    }

    void Disable()
    {
        State& state = GetState();
        state.enabled = false;
        state.capture = nullptr;
        state.user = nullptr;
    }

    Bool IsEnabled()
    {
        return GetState().enabled;
    }

    Bool IsInCall()
    {
        return GetState().depth > 0;
    }

    Bool HasFault()
    {
        return GetState().faulted;
    }

    const ScriptFault& GetFault()
    {
        return GetState().fault;
    }

    void ClearFault()
    {
        State& state = GetState();
        state.faulted = false;
        state.fault = ScriptFault{};
    }

    namespace Detail
    {
        int Filter(const void* exceptionPointers, const char* hook)
        {
            State& state = GetState();
            // 첫 예외만 적는다. 뒤의 것은 앞의 것이 남긴 망가진 상태에서 났을 수 있다.
            if (false == state.faulted)
            {
                ScriptFault fault;
                fault.hook = hook;
                const auto* pointers = static_cast<const ExceptionPointersHead*>(exceptionPointers);
                if (pointers != nullptr && pointers->record != nullptr)
                {
                    fault.code = pointers->record->code;
                    fault.address = static_cast<UInt64>(reinterpret_cast<std::uintptr_t>(pointers->record->address));
                }
                // 스택이 넘쳐 들어온 필터는 남은 스택이 얼마 없다. 거슬러 올라가는 데 컨텍스트 사본을 쓰므로 그때는 하지 않는다.
                if (state.capture != nullptr && fault.code != StackOverflowCode)
                {
                    fault.frameCount = state.capture(state.user, exceptionPointers, fault.frames, ScriptFault::MaxFrames);
                }
                state.fault = fault;
                state.faulted = true;
            }
#if defined(_MSC_VER)
            return EXCEPTION_EXECUTE_HANDLER;
#else
            return 1;
#endif
        }

        void Enter()
        {
            ++GetState().depth;
        }

        void Leave()
        {
            State& state = GetState();
            if (state.depth > 0)
            {
                --state.depth;
            }
        }

        void Recover()
        {
#if defined(_MSC_VER)
            if (GetState().fault.code == StackOverflowCode)
            {
                _resetstkoflw();
            }
#endif
        }
    }
}
