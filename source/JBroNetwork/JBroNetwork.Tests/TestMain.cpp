#include <crtdbg.h>
#include <stdlib.h>

#include <iostream>

int RunWebSocketProtocolTests();
int RunMemorySocketTests();
int RunTransportTests();
int RunSessionTests();
int RunWinsockLoopbackTests();

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

    // 순수 코덕과 인메모리 테스트를 앞에 둔다. 실제 소켓 테스트는 시간이 걸리므로 뒤다.
    if (RunWebSocketProtocolTests() != 0)
    {
        return 1;
    }
    if (RunMemorySocketTests() != 0)
    {
        return 1;
    }
    if (RunTransportTests() != 0)
    {
        return 1;
    }
    if (RunSessionTests() != 0)
    {
        return 1;
    }
    if (RunWinsockLoopbackTests() != 0)
    {
        return 1;
    }
    std::cout << "all network tests passed\n";
    return 0;
}
