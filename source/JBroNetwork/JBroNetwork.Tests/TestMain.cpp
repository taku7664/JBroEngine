#include <crtdbg.h>
#include <stdlib.h>

#include <iostream>

int RunMemorySocketTests();
int RunTransportTests();

int main()
{
    // 단언이 대화상자를 띄우면 사람 없이 도는 자리에서 영원히 멈춘다. 엔진 테스트와 같은 처리다.
    _set_error_mode(_OUT_TO_STDERR);
    for (int report : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
    {
        _CrtSetReportMode(report, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
    }
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    if (RunMemorySocketTests() != 0)
    {
        return 1;
    }
    if (RunTransportTests() != 0)
    {
        return 1;
    }
    std::cout << "all network tests passed\n";
    return 0;
}
