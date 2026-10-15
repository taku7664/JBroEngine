#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/SafePtr.h>

namespace JBro
{
    class EditorApplication;
    class IPlatform;
    class ILocalPipe;

    // **에디터와 디버그 어댑터 사이의 통로**(D-330 3 단계). 에디터가 이름 있는 파이프 `jbro-debug-<pid>` 를 열고, 어댑터가 붙어 글을 주고받는다.
    //
    // 어댑터는 에디터를 세우지 않고 붙어 메모리와 심볼만 읽는다. 중단점은 어댑터가 소스 줄을 주소로 바꿔 이 통로로 넘기고, 엔진이 그 자리에 int 3 을 넣는다.
    // 스크립트가 서면 에디터가 멈춘 자리·그 스레드의 문맥(`CONTEXT` 주소)·스레드 번호를 보내고, 어댑터가 그 문맥으로 스택과 변수를 읽는다.
    //
    // 통로는 한 줄에 한 글이다(줄바꿈으로 끊는다). 막히지 않는다 - 메인 스레드가 프레임마다, 그리고 멈춘 동안의 루프에서 `Poll` 을 부른다.
    //
    // 어댑터 → 에디터:  `setbp <16진주소>`  `clearbp <16진주소>`  `continue`  `stop`
    // 에디터 → 어댑터:  `bp <16진주소> <0|1>`(setbp 답)  `stopped <16진주소> <문맥주소> <스레드>`  `resumed`  `module <0|1> <경로>`  `exited`
    class ScriptDebugServer
    {
    public:
        ScriptDebugServer();
        ~ScriptDebugServer();
        ScriptDebugServer(const ScriptDebugServer&) = delete;
        ScriptDebugServer& operator=(const ScriptDebugServer&) = delete;

        // 통로를 연다. 이미 열려 있거나 못 열면 거짓이다.
        Bool Start(IPlatform& platform, UInt32 processId);
        void Stop();
        Bool IsActive() const;

        // 와 있는 어댑터 명령을 처리한다. 프레임마다, 그리고 멈춘 동안의 루프에서 부른다.
        void Poll(EditorApplication& editor);

        // 스크립트가 중단점에 섰다. `context` 는 멈춘 스레드의 `CONTEXT*`(어댑터가 `.cxr` 로 읽는다)다.
        void NotifyStopped(UInt64 address, const void* context, UInt32 threadId);
        void NotifyResumed();
        // 스크립트 모듈이 실리거나 내려갔다. 어댑터가 그 모듈에 줄 → 주소를 다시 푼다.
        void NotifyModule(Bool loaded, const String& path);
        void NotifyExit();

    private:
        void Send(const String& line);
        void Dispatch(EditorApplication& editor, const String& line);

        OwnerPtr<ILocalPipe> m_pipe;
        String m_inbox;
    };
}
