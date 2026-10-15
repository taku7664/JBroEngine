#include <JBro/Platform/WindowsPlatform.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>

// 스크립트 중단점(D-330 2 단계). 엔진이 코드에 int 3 을 넣고, 걸리면 벡터 예외 처리기(VEH)가 받아 호스트의 처리기를 그 자리에서 부른다.
// OS 디버거로 붙지 않으므로 프로세스의 다른 스레드는 서지 않는다 - 에디터는 처리기 안에서 화면을 계속 그린다.
//
// 넣은 중단점에서 이어 갈 때는 원래 바이트를 되돌리고 트랩 플래그로 명령 하나만 간 뒤(`EXCEPTION_SINGLE_STEP`) int 3 을 다시 넣는다.
// 상태는 프로세스에 하나다. VEH 가 정적 함수라 인스턴스를 거치지 않는다.

namespace JBro
{
    namespace
    {
        constexpr std::uint8_t Int3 = 0xCC;
        constexpr DWORD TrapFlag = 0x100;
        constexpr std::size_t MaxBreakpoints = 256;

        struct Breakpoint
        {
            std::uint64_t address = 0;
            std::uint8_t original = 0;
        };

        struct BreakState
        {
            CodeBreakHandler handler = nullptr;
            void* user = nullptr;
            PVOID registration = nullptr;
            DWORD thread = 0;
            Breakpoint breakpoints[MaxBreakpoints] = {};
            std::size_t count = 0;
            // 한 명령을 간 뒤 int 3 을 다시 넣을 자리다. 0 이면 없다.
            std::uint64_t rearm = 0;
        };

        BreakState& GetBreakState()
        {
            static BreakState state;
            return state;
        }

        Breakpoint* FindBreakpoint(std::uint64_t address)
        {
            BreakState& state = GetBreakState();
            for (std::size_t index = 0; index < state.count; ++index)
            {
                if (state.breakpoints[index].address == address)
                {
                    return &state.breakpoints[index];
                }
            }
            return nullptr;
        }

        Bool IsExecutable(std::uint64_t address)
        {
            MEMORY_BASIC_INFORMATION info = {};
            if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) == 0 || info.State != MEM_COMMIT)
            {
                return false;
            }
            const DWORD execute = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            return (info.Protect & execute) != 0 && (info.Protect & PAGE_GUARD) == 0;
        }

        // 코드 한 바이트를 바꾼다. 예외 처리기 안에서도 부른다 - 할당하지 않는다.
        Bool WriteCodeByte(std::uint64_t address, std::uint8_t value, std::uint8_t* previous)
        {
            auto* code = reinterpret_cast<std::uint8_t*>(address);
            DWORD protection = 0;
            if (FALSE == VirtualProtect(code, 1, PAGE_EXECUTE_READWRITE, &protection))
            {
                return false;
            }
            if (previous != nullptr)
            {
                *previous = *code;
            }
            *code = value;
            DWORD restored = 0;
            VirtualProtect(code, 1, protection, &restored);
            FlushInstructionCache(GetCurrentProcess(), code, 1);
            return true;
        }

        LONG CALLBACK OnBreakException(EXCEPTION_POINTERS* info)
        {
            BreakState& state = GetBreakState();
            if (state.handler == nullptr || info == nullptr || info->ExceptionRecord == nullptr || info->ContextRecord == nullptr
                || GetCurrentThreadId() != state.thread)
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }
            CONTEXT* context = info->ContextRecord;
            const DWORD code = info->ExceptionRecord->ExceptionCode;
#if defined(_M_X64)
            if (code == EXCEPTION_SINGLE_STEP && state.rearm != 0)
            {
                const std::uint64_t address = state.rearm;
                state.rearm = 0;
                if (FindBreakpoint(address) != nullptr)
                {
                    WriteCodeByte(address, Int3, nullptr);
                }
                context->EFlags &= ~TrapFlag;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
            if (code != EXCEPTION_BREAKPOINT)
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }
            // x64 Windows 는 int 3 의 예외에서 `Rip` 를 int 3 자리로 되돌려 준다.
            const std::uint64_t address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress));
            const Bool inserted = FindBreakpoint(address) != nullptr;
            const Bool embedded = false == inserted && *reinterpret_cast<const std::uint8_t*>(address) == Int3;
            if (false == inserted && false == embedded)
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }
            CodeBreak hit;
            hit.address = UInt64(address);
            hit.embedded = embedded;
            hit.context = context;
            hit.threadId = UInt32(static_cast<std::uint32_t>(GetCurrentThreadId()));
            const CodeBreakAction action = state.handler(state.user, hit);
            if (action == CodeBreakAction::Stop || (action == CodeBreakAction::Pass && embedded))
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }
            // 멈춘 동안 처리기가 이 중단점을 뺐을 수 있다. 그러면 원래 바이트가 이미 돌아와 있어 그 자리에서 이어 가면 된다.
            if (const Breakpoint* breakpoint = FindBreakpoint(address))
            {
                WriteCodeByte(address, breakpoint->original, nullptr);
                context->Rip = address;
                context->EFlags |= TrapFlag;
                state.rearm = address;
            }
            else if (embedded)
            {
                context->Rip = address + 1;
            }
            else
            {
                context->Rip = address;
            }
            return EXCEPTION_CONTINUE_EXECUTION;
#else
            (void)context;
            (void)code;
            return EXCEPTION_CONTINUE_SEARCH;
#endif
        }
    }

    Bool WindowsPlatform::SetCodeBreakHandler(CodeBreakHandler handler, void* user)
    {
#if defined(_M_X64)
        BreakState& state = GetBreakState();
        if (handler == nullptr)
        {
            ClearCodeBreakpoints();
            if (state.registration != nullptr)
            {
                RemoveVectoredExceptionHandler(state.registration);
                state.registration = nullptr;
            }
            state.handler = nullptr;
            state.user = nullptr;
            state.rearm = 0;
            return true;
        }
        if (state.registration == nullptr)
        {
            // 맨 앞에 건다. 스크립트를 감싼 SEH 보다 먼저 받아야 그 자리에서 설 수 있다.
            state.registration = AddVectoredExceptionHandler(1, &OnBreakException);
            if (state.registration == nullptr)
            {
                return false;
            }
        }
        state.handler = handler;
        state.user = user;
        state.thread = GetCurrentThreadId();
        return true;
#else
        (void)handler;
        (void)user;
        return false;
#endif
    }

    Bool WindowsPlatform::SetCodeBreakpoint(UInt64 address)
    {
        BreakState& state = GetBreakState();
        const std::uint64_t raw = static_cast<std::uint64_t>(address);
        if (FindBreakpoint(raw) != nullptr)
        {
            return true;
        }
        if (state.count >= MaxBreakpoints || false == IsExecutable(raw))
        {
            return false;
        }
        Breakpoint& breakpoint = state.breakpoints[state.count];
        if (false == WriteCodeByte(raw, Int3, &breakpoint.original))
        {
            return false;
        }
        // 이미 int 3 인 자리(코드에 든 `__debugbreak`)에 넣으면 원래 바이트도 int 3 이다. 그대로 두어도 뺄 때 같은 바이트로 돌아간다.
        breakpoint.address = raw;
        ++state.count;
        return true;
    }

    Bool WindowsPlatform::ClearCodeBreakpoint(UInt64 address)
    {
        BreakState& state = GetBreakState();
        const std::uint64_t raw = static_cast<std::uint64_t>(address);
        for (std::size_t index = 0; index < state.count; ++index)
        {
            if (state.breakpoints[index].address != raw)
            {
                continue;
            }
            // 한 명령을 가는 중이면 지금 원래 바이트가 들어 있다. 다시 넣지 않게만 한다.
            if (state.rearm != raw)
            {
                WriteCodeByte(raw, state.breakpoints[index].original, nullptr);
            }
            if (state.rearm == raw)
            {
                state.rearm = 0;
            }
            state.breakpoints[index] = state.breakpoints[state.count - 1];
            --state.count;
            return true;
        }
        return false;
    }

    void WindowsPlatform::ClearCodeBreakpoints()
    {
        BreakState& state = GetBreakState();
        while (state.count > 0)
        {
            ClearCodeBreakpoint(UInt64(state.breakpoints[state.count - 1].address));
        }
    }
}
