#pragma once

#include "DapChannel.h"
#include "Json.h"

#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/SafePtr.h>
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

    class SessionEvents;
    class SessionOutput;

    // **스크립트 DLL 을 디버깅하는 DAP 세션이다**(D-319). 떠 있는 에디터에 DbgEng 으로 붙고, 스크립트 소스 줄에 중단점을 건다.
    //
    // 스레드는 둘이다. 읽기 스레드는 요청을 큐에 넣기만 하고, DbgEng 은 이 객체를 만든 스레드(엔진 스레드)에서만 부른다 - DbgEng 은 한 스레드에서 쓴다.
    // 대상이 도는 동안 온 요청은 대상을 잠깐 세우고(`DebugBreakProcess`) 처리한 뒤 다시 돌린다. 사용자에게는 멈춤을 알리지 않는다.
    //
    // 에디터는 스크립트 DLL 을 `GameScript.dll.jbro.<pid>.<세대>.dll` 복사본으로 싣고, 다시 빌드하면 새 복사본을 싣는다. 그래서 중단점은
    // 모듈이 아니라 소스 줄로 들고 있다가, 스크립트 모듈이 실릴 때마다 그 모듈에 다시 건다.
    class DebugSession
    {
    public:
        explicit DebugSession(DapChannel& channel);
        ~DebugSession();
        DebugSession(const DebugSession&) = delete;
        DebugSession& operator=(const DebugSession&) = delete;

        // 클라이언트가 끊거나 입력이 끝날 때까지 돈다. 종료 코드를 돌려준다.
        Int32 Run();

        // DbgEng 이벤트 콜백이 부른다(엔진 스레드).
        ULONG OnBreakpoint(IDebugBreakpoint2* breakpoint);
        ULONG OnException(const EXCEPTION_RECORD64& record, Bool firstChance);
        ULONG OnLoadModule(PCWSTR imageName, ULONG64 baseOffset);
        void OnUnloadModule(ULONG64 baseOffset);
        void OnExitProcess(ULONG exitCode);

    private:
        enum class TargetState : std::uint8_t
        {
            // 아직 붙지 않았다.
            None,
            Stopped,
            Running,
            // 대상이 끝났거나 떼어 냈다.
            Gone
        };

        // 대상을 다시 돌리는 방법이다. 사용자 모르게 세웠다가 다시 돌릴 때 같은 것을 쓴다.
        enum class ResumeKind : std::uint8_t
        {
            Go,
            StepOver,
            StepInto,
            StepOut
        };

        struct SourceBreakpoint
        {
            // 클라이언트가 보낸 그대로의 경로다.
            String path;
            Int32 line = 0;
            Int32 id = 0;
            // 실제로 멈출 줄이다(빈 줄이면 다음 코드 줄로 옮겨진다). 아직 어느 모듈에도 걸리지 않았으면 0 이다.
            Int32 resolvedLine = 0;
            // 모듈마다 건 DbgEng 중단점 번호다.
            Array<ULONG> engineIds;
        };

        struct ScriptModule
        {
            ULONG64 base = 0;
            // DbgEng 의 모듈 이름(`GameScript_dll_jbro_123_2`)이다. 중단점 식에 쓴다.
            String name;
        };

        // `variablesReference` 하나가 가리키는 것이다. 대상이 다시 돌면 모두 버린다.
        struct VariableHandle
        {
            ComPtr<IModelObject> object;
            // 아직 값을 얻지 않은 식이다(범위의 지역 변수 목록). 비었으면 `object` 를 쓴다.
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
        void OnResume(const JsonValue& request, ResumeKind kind);
        void OnDisconnect(const JsonValue& request);

        // 대상이 섰다. 까닭을 가려 사용자에게 알리거나, 몰래 세운 것이면 큐를 처리하고 다시 돌린다.
        void HandleBreak();
        void Resume(ResumeKind kind);
        void RequestBreakIn();
        void Detach();
        void FinishTarget(Int32 exitCode);

        Bool IsScriptImage(const String& imagePath) const;
        void AddScriptModule(ULONG64 base);
        void RemoveScriptModule(ULONG64 base);
        // 이 중단점을 실린 스크립트 모듈 하나에 건다. 걸렸으면 참이다.
        Bool PlaceInModule(SourceBreakpoint& breakpoint, const ScriptModule& module);
        void ClearEngineBreakpoints(SourceBreakpoint& breakpoint);
        JsonValue DescribeBreakpoint(const SourceBreakpoint& breakpoint) const;

        Bool SelectThread(Int64 systemId);
        Bool SelectFrame(Int64 frameId);
        void ReleaseHandles();
        Int32 AddHandle(VariableHandle handle);
        Bool Evaluate(const String& expression, ComPtr<IModelObject>& result, String& error);
        void CollectChildren(IModelObject* object, Array<Child>& out);
        JsonValue DescribeVariable(const String& name, IModelObject* object);

        DapChannel& m_channel;

        // 읽기 스레드와 나누는 것.
        std::mutex m_queueLock;
        std::condition_variable m_queueSignal;
        std::deque<JsonValue> m_queue;
        std::thread m_reader;
        std::atomic<bool> m_inputClosed{false};

        // 아래는 엔진 스레드만 만진다.
        ComPtr<IDebugClient5> m_client;
        ComPtr<IDebugControl4> m_control;
        ComPtr<IDebugSymbols3> m_symbols;
        ComPtr<IDebugSystemObjects4> m_system;
        ComPtr<IDataModelManager> m_dataModel;
        ComPtr<IDebugHost> m_host;
        ComPtr<IDebugHostEvaluator2> m_evaluator;
        // DbgEng 에 넘기는 이벤트 콜백이다. 참조 수를 세지만 지우는 것은 이 세션이다.
        OwnerPtr<SessionEvents> m_events;
        OwnerPtr<SessionOutput> m_output;
        HANDLE m_process = nullptr;
        ULONG m_processId = 0;

        TargetState m_state = TargetState::None;
        Bool m_quit = false;
        Int32 m_exitCode = 0;
        // 스크립트 모듈로 볼 이미지 이름의 머리다(`GameScript`). 파일 이름이 이것으로 시작하면 스크립트다.
        Array<String> m_scriptPrefixes;
        Array<ScriptModule> m_scriptModules;
        Array<SourceBreakpoint> m_breakpoints;
        Int32 m_nextBreakpointId = 1;

        // 마지막 멈춤의 까닭이다. `HandleBreak` 가 읽고 지운다.
        Array<ULONG64> m_loadedScriptBases;
        ULONG m_hitBreakpointId = DEBUG_ANY_ID;
        Bool m_hitBreakIn = false;
        Bool m_hitException = false;
        String m_exceptionText;
        Bool m_exited = false;

        // 몰래 세우려고 보낸 끼어들기 수다. 끼어들기 예외가 오면 하나씩 줄인다.
        Int32 m_pendingBreakIns = 0;
        Bool m_pauseRequested = false;
        ResumeKind m_resumeKind = ResumeKind::Go;
        Bool m_stepping = false;
        ULONG m_stepOutBreakpointId = DEBUG_ANY_ID;
        // 대상이 도는 동안 와서 멈춘 뒤로 미룬 요청이다.
        Array<JsonValue> m_deferred;

        Array<VariableHandle> m_handles;
        // 마지막으로 멈춘 스레드(시스템 번호)다.
        ULONG m_stoppedThread = 0;
    };
}
