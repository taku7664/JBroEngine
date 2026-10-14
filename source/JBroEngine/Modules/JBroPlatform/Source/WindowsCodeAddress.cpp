#include <JBro/Platform/WindowsPlatform.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DbgHelp.h>

#pragma comment(lib, "Dbghelp.lib")

namespace JBro
{
    namespace
    {
        String ToUtf8(const wchar_t* wide)
        {
            String result;
            if (wide == nullptr || wide[0] == L'\0')
            {
                return result;
            }
            const int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
            if (length <= 1)
            {
                return result;
            }
            result.resize(static_cast<std::size_t>(length - 1));
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), length, nullptr, nullptr);
            return result;
        }

        // dbghelp 는 프로세스에 한 번 연다. 스크립트 DLL 은 그 뒤에 실리고 다시 빌드할 때마다 새 사본이 실리므로, 찾기 전에 모듈 목록을 새로 읽는다.
        Bool EnsureSymbols()
        {
            static Bool opened = false;
            static Bool failed = false;
            if (failed)
            {
                return false;
            }
            HANDLE process = GetCurrentProcess();
            if (false == opened)
            {
                SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
                if (FALSE == SymInitializeW(process, nullptr, TRUE))
                {
                    failed = true;
                    return false;
                }
                opened = true;
                return true;
            }
            SymRefreshModuleList(process);
            return true;
        }
    }

    UInt32 WindowsPlatform::CaptureFaultFrames(const void* exceptionPointers, UInt64* frames, UInt32 capacity)
    {
#if defined(_M_X64)
        const auto* pointers = static_cast<const EXCEPTION_POINTERS*>(exceptionPointers);
        if (pointers == nullptr || pointers->ContextRecord == nullptr || frames == nullptr)
        {
            return 0;
        }
        // 예외 자리의 레지스터에서 시작해 함수 테이블로 한 칸씩 거슬러 올라간다. dbghelp 를 쓰지 않는다 - 필터 안이라 가볍고 잠그지 않아야 한다.
        CONTEXT context = *pointers->ContextRecord;
        UInt32 count = 0;
        while (count < capacity && context.Rip != 0)
        {
            frames[count] = static_cast<JBro::UInt64>(context.Rip);
            ++count;
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
            if (function == nullptr)
            {
                // 함수 표가 없는 자리(잎 함수, 또는 null 을 불러 들어간 자리)는 돌아갈 주소가 스택 꼭대기에 있다.
                if (context.Rsp == 0)
                {
                    break;
                }
                context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
                context.Rsp += 8;
                continue;
            }
            void* handlerData = nullptr;
            DWORD64 establisherFrame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisherFrame, nullptr);
        }
        return count;
#else
        (void)exceptionPointers;
        (void)frames;
        (void)capacity;
        return 0;
#endif
    }

    Bool WindowsPlatform::DescribeCodeAddress(UInt64 address, CodeLocation& location)
    {
        location = CodeLocation{};
        HMODULE module = nullptr;
        if (FALSE == GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(static_cast<std::uintptr_t>(address.Value)), &module))
        {
            return false;
        }
        wchar_t path[MAX_PATH * 4];
        const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
        if (length == 0 || length >= sizeof(path) / sizeof(path[0]))
        {
            return false;
        }
        location.module = ToUtf8(path);
        if (false == EnsureSymbols())
        {
            return true;
        }
        HANDLE process = GetCurrentProcess();
        alignas(SYMBOL_INFOW) std::byte symbolBuffer[sizeof(SYMBOL_INFOW) + 512 * sizeof(wchar_t)] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFOW*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
        symbol->MaxNameLen = 512;
        DWORD64 displacement = 0;
        if (SymFromAddrW(process, address.Value, &displacement, symbol))
        {
            location.function = ToUtf8(symbol->Name);
        }
        IMAGEHLP_LINEW64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        if (SymGetLineFromAddrW64(process, address.Value, &lineDisplacement, &line))
        {
            location.file = ToUtf8(line.FileName);
            location.line = static_cast<JBro::UInt32>(line.LineNumber);
        }
        return true;
    }
}
