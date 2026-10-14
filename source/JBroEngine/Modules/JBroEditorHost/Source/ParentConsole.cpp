#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdio>

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
