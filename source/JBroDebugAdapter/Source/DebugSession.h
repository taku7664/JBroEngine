#pragma once

#include "DapChannel.h"
#include "Json.h"

#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>

#include <Windows.h>
#include <DbgEng.h>
#include <DbgModel.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace JBro::DebugAdapter
{
    template<typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    class SessionOutput;

    // **스크립트 DLL 을 디버깅하는 DAP 세션이다**(D-319, D-330 3 단계). 떠 있는 에디터에 **세우지 않고**(noninvasive no-suspend) 붙어
    // 메모리와 심볼만 읽는다 - 에디터는 멈추지 않는다. 중단점·멈춤·재개·모듈 실림은 에디터와의 파이프(`jbro-debug-<pid>`)로 주고받는다.
    //
    // 중단점은 소스 줄을 주소로 바꿔 에디터에 넘기고(엔진이 int 3 을 넣는다), 스크립트가 서면 에디터가 멈춘 스레드의 `CONTEXT` 주소를 보낸다.
    // 그 문맥으로 `.cxr` 한 뒤 스택과 변수를 읽는다 - noninvasive 라 디버그 이벤트는 오지 않는다.
    //
    // 읽기 스레드는 DAP 요청을 큐에 넣기만 하고, DbgEng 과 파이프는 이 객체를 만든 스레드에서만 쓴다.
    class DebugSession
    {
    public:
        explicit DebugSession(DapChannel& channel);
        ~DebugSession();
        DebugSession(const DebugSession&) = delete;
        DebugSession& operator=(const DebugSession&) = delete;

        Int32 Run();

    private:
        enum class State : std::uint8_t
        {
            None,
            // 붙었고 스크립트가 돌고 있다(에디터는 멈추지 않는다).
            Running,
            // 스크립트가 중단점에 섰다. 문맥으로 읽을 수 있다.
            Stopped,
            Gone
        };

        struct SourceBreakpoint
        {
            String path;
            Int32 line = 0;
            Int32 id = 0;
            // 실제로 멈출 줄이다. 아직 어느 모듈에서도 풀리지 않았으면 0 이다.
            Int32 resolvedLine = 0;
            // 지금 실린 복사본에서 그 줄의 주소다. 0 이면 풀리지 않았다.
            ULONG64 address = 0;
            // 에디터가 이 주소에 중단점을 넣었다고 답했는가.
            Bool armed = false;
        };

        struct ScriptModule
        {
            ULONG64 base = 0;
            String name;
        };

        struct VariableHandle
        {
            ComPtr<IModelObject> object;
            String expression;
        };

        struct Child
        {
            String name;
            ComPtr<IModelObject> value;
        };

        void ReadLoop();
        void Dispatch(const JsonValue& request);
        Bool PopRequest(JsonValue& request, Bool wait);

        void OnInitialize(const JsonValue& request);
        void OnAttach(const JsonValue& request);
        void OnSetBreakpoints(const JsonValue& request);
        void OnConfigurationDone(const JsonValue& request);
        void OnThreads(const JsonValue& request);
        void OnStackTrace(const JsonValue& request);
        void OnScopes(const JsonValue& request);
        void OnVariables(const JsonValue& request);
        void OnEvaluate(const JsonValue& request);
        void OnResume(const JsonValue& request, const char* pipeCommand);
        void OnDisconnect(const JsonValue& request);

        // ── 에디터와의 파이프 ──
        Bool ConnectEditorPipe(ULONG processId);
        void PipeSend(const String& line);
        // 와 있는 줄을 모두 처리한다.
        void PumpPipe();
        void OnEditorLine(const String& line);
        void OnStoppedFromEditor(ULONG64 address, ULONG64 contextAddress, ULONG threadId);
        void OnModuleFromEditor(Bool loaded, const String& path);

        // ── 심볼과 중단점 ──
        Bool IsScriptImage(const String& imagePath) const;
        void EnumerateScriptModules();
        void AddScriptModule(ULONG64 base);
        // 이 중단점을 실린 모듈에서 풀어 주소를 얻고, 에디터에 걸어 달라 한다.
        Bool ResolveAndArm(SourceBreakpoint& breakpoint);
        void DisarmBreakpoint(SourceBreakpoint& breakpoint);
        JsonValue DescribeBreakpoint(const SourceBreakpoint& breakpoint) const;

        // ── 변수 ──
        Bool SelectFrame(Int64 frameId);
        void ReleaseHandles();
        Int32 AddHandle(VariableHandle handle);
        Bool Evaluate(const String& expression, ComPtr<IModelObject>& result, String& error);
        void CollectChildren(IModelObject* object, Array<Child>& out);
        JsonValue DescribeVariable(const String& name, IModelObject* object);

        void FinishTarget(Int32 exitCode);

        DapChannel& m_channel;

        std::mutex m_queueLock;
        std::condition_variable m_queueSignal;
        std::deque<JsonValue> m_queue;
        std::thread m_reader;
        std::atomic<bool> m_inputClosed{false};

        ComPtr<IDebugClient5> m_client;
        ComPtr<IDebugControl4> m_control;
        ComPtr<IDebugSymbols3> m_symbols;
        ComPtr<IDebugSystemObjects4> m_system;
        ComPtr<IDataModelManager> m_dataModel;
        ComPtr<IDebugHost> m_host;
        ComPtr<IDebugHostEvaluator2> m_evaluator;
        OwnerPtr<SessionOutput> m_output;
        ULONG m_processId = 0;

        // 에디터와의 파이프.
        HANDLE m_pipe = INVALID_HANDLE_VALUE;
        String m_pipeInbox;

        State m_state = State::None;
        Bool m_quit = false;
        Int32 m_exitCode = 0;
        Array<String> m_scriptPrefixes;
        Array<ScriptModule> m_scriptModules;
        Array<SourceBreakpoint> m_breakpoints;
        Int32 m_nextBreakpointId = 1;

        // 지금 멈춘 자리.
        ULONG64 m_stoppedContext = 0;
        ULONG m_stoppedThread = 0;

        Array<VariableHandle> m_handles;
    };
}
