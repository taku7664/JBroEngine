#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdio>
#include <JBro/Types/String.h>

// **에디터는 창 프로그램이다**(script-user-scenario Q2). 콘솔 프로그램이던 때는 런처나 탐색기로 띄우면 검은 콘솔 창이 같이 떴다.
// 터미널에서 띄운 사람은 `--help` 와 끝날 때의 줄을 보아야 하므로, 부모에게 콘솔이 있으면 거기에 붙는다.
// 출력이 이미 파일이나 파이프로 돌려져 있으면(시험·스크립트가 그렇게 띄운다) 그 핸들을 그대로 쓴다 - 붙지 않는다.
void AttachToParentConsole()
{
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output != nullptr && output != INVALID_HANDLE_VALUE && GetFileType(output) != FILE_TYPE_UNKNOWN)
    {
        return;
    }
    if (FALSE == AttachConsole(ATTACH_PARENT_PROCESS))
    {
        return;
    }
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
}

// 넓은 문자 인자 하나를 UTF-8 로 바꾼다(D-389). `EditorHostMain.cpp` 의 `wmain` 이 부른다.
JBro::String ToUtf8Argument(const wchar_t* argument)
{
    if (argument == nullptr || argument[0] == L'\0')
    {
        return JBro::String();
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, argument, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1)
    {
        return JBro::String();
    }
    JBro::String utf8(static_cast<std::size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, argument, -1, utf8.data(), bytes, nullptr, nullptr);
    return utf8;
}
