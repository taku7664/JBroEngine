#include <Windows.h>

#include <cstdio>
#include <string>

// 디버그 어댑터 시험이 붙는 대상이다(D-319). 에디터처럼 스크립트 DLL 을 세대마다 다른 이름의 복사본으로 싣는다.
// 시험과는 작업 폴더의 파일로만 말한다:
//   run     있으면 매 10 ms 마다 스크립트를 부른다(없으면 부르지 않는다 - 중단점을 건 채 다시 싣기 위해).
//   reload  있으면 지우고 DLL 을 내린 뒤 다음 세대 복사본을 싣는다. 실은 세대를 `generation` 에 적는다.
//   quit    있으면 끝낸다. 시험이 사라져도 2 분 뒤에는 혼자 끝난다.
namespace
{
    using TickFunction = int (*)(int);

    bool Exists(const std::wstring& path)
    {
        return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    HMODULE Load(const std::wstring& folder, const std::wstring& source, int generation, TickFunction& tick)
    {
        const std::wstring copy = folder + L"\\TargetScript.gen" + std::to_wstring(generation) + L".dll";
        CopyFileW(source.c_str(), copy.c_str(), FALSE);
        HMODULE module = LoadLibraryW(copy.c_str());
        tick = module != nullptr ? reinterpret_cast<TickFunction>(GetProcAddress(module, "TargetTick")) : nullptr;
        const std::wstring marker = folder + L"\\generation";
        HANDLE file = CreateFileW(marker.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            char text[16] = {};
            const int length = std::snprintf(text, sizeof(text), "%d", tick != nullptr ? generation : 0);
            DWORD written = 0;
            WriteFile(file, text, static_cast<DWORD>(length), &written, nullptr);
            CloseHandle(file);
        }
        return module;
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2)
    {
        return 2;
    }
    const std::wstring folder = argv[1];
    wchar_t self[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
    std::wstring source = self;
    source = source.substr(0, source.find_last_of(L"\\/") + 1) + L"JBroDebugAdapterTargetScript.dll";

    int generation = 1;
    TickFunction tick = nullptr;
    HMODULE module = Load(folder, source, generation, tick);
    if (tick == nullptr)
    {
        return 3;
    }
    int frame = 0;
    volatile int sink = 0;
    const ULONGLONG deadline = GetTickCount64() + 120000;
    while (GetTickCount64() < deadline && false == Exists(folder + L"\\quit"))
    {
        if (Exists(folder + L"\\reload"))
        {
            // 지워질 때까지 다시 해 본다. 갓 만든 파일은 다른 프로세스(백신 검사 따위)가 잠깐 쥐고 있어 지우기가 실패할 수 있고,
            // 그러면 다음 바퀴에 한 번 더 다시 싣는다.
            for (int attempt = 0; attempt < 200 && FALSE == DeleteFileW((folder + L"\\reload").c_str()); ++attempt)
            {
                Sleep(10);
            }
            FreeLibrary(module);
            generation = generation + 1;
            module = Load(folder, source, generation, tick);
            if (tick == nullptr)
            {
                return 4;
            }
        }
        if (Exists(folder + L"\\run"))
        {
            sink = tick(frame);
            frame = frame + 1;
        }
        Sleep(10);
    }
    (void)sink;
    FreeLibrary(module);
    return 0;
}
