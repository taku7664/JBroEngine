#include <JBro/Platform/WindowsPlatform.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DbgHelp.h>

#pragma comment(lib, "Dbghelp.lib")

#include <cstdint>
#include <cwchar>

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
            // 한 줄 가기(D-330 4 단계). 처리기가 `RequestCodeStep` 으로 신청하면 돌아간 뒤 트랩 플래그로 줄이 바뀔 때까지 간다.
            bool stepping = false;
            bool stepRequested = false;
            CodeStepKind stepKind = CodeStepKind::Into;
            std::uint64_t stepStartRsp = 0;
            unsigned long stepStartLine = 0;
            wchar_t stepStartFile[260] = {};
            std::size_t stepCount = 0;
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

#if defined(_M_X64)
        // 심볼을 한 번 연다. dbghelp 는 프로세스에 하나이고, 코드 주소를 소스 줄로 바꾸는 쪽(WindowsCodeAddress)이 이미 열었을 수 있다.
        void EnsureStepSymbols()
        {
            static bool opened = false;
            if (false == opened)
            {
                opened = true;
                SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
                SymInitializeW(GetCurrentProcess(), nullptr, TRUE);
            }
        }

        // 주소의 소스 파일·줄을 읽는다. 없으면 거짓이다. 한 줄 가기가 명령마다 부른다 - 느리지만 디버깅에서만이다.
        bool LineFor(std::uint64_t address, unsigned long& line, wchar_t* file, std::size_t capacity)
        {
            EnsureStepSymbols();
            IMAGEHLP_LINEW64 info = {};
            info.SizeOfStruct = sizeof(info);
            DWORD displacement = 0;
            if (FALSE == SymGetLineFromAddrW64(GetCurrentProcess(), address, &displacement, &info) || info.FileName == nullptr)
            {
                return false;
            }
            line = info.LineNumber;
            ::wcsncpy_s(file, capacity, info.FileName, _TRUNCATE);
            file[capacity - 1] = L'\0';
            return true;
        }

        void BeginStep(BreakState& state, CONTEXT* context)
        {
            state.stepping = true;
            state.stepRequested = false;
            state.stepStartRsp = context->Rsp;
            state.stepStartLine = 0;
            state.stepStartFile[0] = L'\0';
            LineFor(context->Rip, state.stepStartLine, state.stepStartFile, 260);
            state.stepCount = 0;
            context->EFlags |= TrapFlag;
        }

        // 한 줄 가기가 지금 자리에서 서야 하는가.
        bool StepShouldStop(BreakState& state, CONTEXT* context)
        {
            if (++state.stepCount > 2000000)
            {
                return true;
            }
            const bool returned = context->Rsp > state.stepStartRsp;
            const bool deeper = context->Rsp < state.stepStartRsp;
            unsigned long line = 0;
            wchar_t file[260] = {};
            const bool haveLine = LineFor(context->Rip, line, file, 260);
            const bool lineChanged = haveLine && (line != state.stepStartLine || std::wcscmp(file, state.stepStartFile) != 0);
            switch (state.stepKind)
            {
            case CodeStepKind::Out:
                return returned;
            case CodeStepKind::Over:
                return false == deeper && (lineChanged || returned);
            case CodeStepKind::Into:
            default:
                return lineChanged || returned;
            }
        }

        // 처리기가 `Continue` 를 돌려준 뒤, 신청된 한 줄 가기가 있으면 걸고, 없으면 트랩 플래그를 끈다.
        LONG AfterContinue(BreakState& state, CONTEXT* context)
        {
            if (state.stepRequested)
            {
                BeginStep(state, context);
            }
            else
            {
                context->EFlags &= ~TrapFlag;
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }
#endif

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
                // 중단점에서 한 줄 가기를 신청했으면, 그 명령을 지난 지금부터 트랩 플래그로 이어 간다.
                if (state.stepping)
                {
                    context->EFlags |= TrapFlag;
                }
                else
                {
                    context->EFlags &= ~TrapFlag;
                }
                return EXCEPTION_CONTINUE_EXECUTION;
            }
            if (code == EXCEPTION_SINGLE_STEP && state.stepping)
            {
                if (false == StepShouldStop(state, context))
                {
                    context->EFlags |= TrapFlag;
                    return EXCEPTION_CONTINUE_EXECUTION;
                }
                state.stepping = false;
                context->EFlags &= ~TrapFlag;
                CodeBreak hit;
                hit.address = UInt64(context->Rip);
                hit.embedded = false;
                hit.context = context;
                hit.threadId = UInt32(static_cast<std::uint32_t>(GetCurrentThreadId()));
                const CodeBreakAction action = state.handler(state.user, hit);
                if (action == CodeBreakAction::Stop)
                {
                    // 한 줄 가기 자리에는 int 3 이 없어 예외로 세울 수 없다. 이어 가되 그 뒤의 보호가 받게 둔다.
                    context->EFlags &= ~TrapFlag;
                    return EXCEPTION_CONTINUE_EXECUTION;
                }
                return AfterContinue(state, context);
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
                // 한 줄 가기를 신청했으면, 이 중단점 명령을 지난 뒤부터 걸리게 시작점만 잡아 둔다.
                if (state.stepRequested)
                {
                    state.stepping = true;
                    state.stepRequested = false;
                    state.stepStartRsp = context->Rsp;
                    state.stepStartLine = 0;
                    state.stepStartFile[0] = L'\0';
                    LineFor(address, state.stepStartLine, state.stepStartFile, 260);
                    state.stepCount = 0;
                }
                return EXCEPTION_CONTINUE_EXECUTION;
            }
            if (embedded)
            {
                context->Rip = address + 1;
            }
            else
            {
                context->Rip = address;
            }
            return AfterContinue(state, context);
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

    void WindowsPlatform::RequestCodeStep(CodeStepKind kind)
    {
#if defined(_M_X64)
        BreakState& state = GetBreakState();
        state.stepRequested = true;
        state.stepKind = kind;
#else
        (void)kind;
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
