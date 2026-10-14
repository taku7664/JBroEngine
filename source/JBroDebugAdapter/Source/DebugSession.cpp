#include "DebugSession.h"

#include "Text.h"

#include <DbgHelp.h>
#include <TlHelp32.h>

#include <cstdio>
#include <cwchar>

namespace JBro::DebugAdapter
{
    namespace
    {
        // 한 번에 펴 보이는 자식의 상한이다. 큰 배열을 펴다 클라이언트가 멈추지 않게 한다.
        constexpr std::size_t MaxChildren = 1000;
        // 스택을 이만큼까지만 읽는다.
        constexpr ULONG MaxFrames = 256;
        // 프레임 번호 = 스레드 시스템 번호 * 이 수 + 프레임 차례.
        constexpr Int64 FramesPerThread = 1000;

        // 이 실행 파일이 있는 폴더다(끝에 구분자). natvis 와 기본 기호 경로의 자리다.
        String AdapterFolder()
        {
            wchar_t self[MAX_PATH * 2] = {};
            GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
            const String path = ToUtf8(self);
            return String(path.substr(0, path.find_last_of("\\/") + 1));
        }

        String FileNameOf(const String& path)
        {
            const std::size_t slash = path.find_last_of("\\/");
            return slash == String::npos ? path : String(path.substr(slash + 1));
        }

        Bool StartsWithNoCase(const String& text, const String& prefix)
        {
            if (text.size() < prefix.size())
            {
                return false;
            }
            for (std::size_t index = 0; index < prefix.size(); ++index)
            {
                const char a = text[index];
                const char b = prefix[index];
                const char lowerA = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
                const char lowerB = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
                if (lowerA != lowerB)
                {
                    return false;
                }
            }
            return true;
        }

        // 실행 파일 이름으로 떠 있는 프로세스를 찾는다. 여럿이면 가장 늦게 뜬 것이다(번호가 늘 늘지는 않지만, 스냅숏 차례는 만든 차례다).
        ULONG FindProcessByName(const String& name)
        {
            const std::wstring wide = ToWide(name);
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot == INVALID_HANDLE_VALUE)
            {
                return 0;
            }
            PROCESSENTRY32W entry = {};
            entry.dwSize = sizeof(entry);
            ULONG found = 0;
            for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
            {
                if (_wcsicmp(entry.szExeFile, wide.c_str()) == 0)
                {
                    found = entry.th32ProcessID;
                }
            }
            CloseHandle(snapshot);
            return found;
        }

        String FormatVariant(const VARIANT& value)
        {
            char text[64] = {};
            switch (value.vt)
            {
            case VT_BOOL:
                return value.boolVal != VARIANT_FALSE ? "true" : "false";
            case VT_I1:
                std::snprintf(text, sizeof(text), "%d", static_cast<int>(value.cVal));
                break;
            case VT_UI1:
                std::snprintf(text, sizeof(text), "%u", static_cast<unsigned int>(value.bVal));
                break;
            case VT_I2:
                std::snprintf(text, sizeof(text), "%d", static_cast<int>(value.iVal));
                break;
            case VT_UI2:
                std::snprintf(text, sizeof(text), "%u", static_cast<unsigned int>(value.uiVal));
                break;
            case VT_I4:
            case VT_INT:
                std::snprintf(text, sizeof(text), "%ld", static_cast<long>(value.lVal));
                break;
            case VT_UI4:
            case VT_UINT:
                std::snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(value.ulVal));
                break;
            case VT_I8:
                std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(value.llVal));
                break;
            case VT_UI8:
                std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value.ullVal));
                break;
            case VT_R4:
                std::snprintf(text, sizeof(text), "%g", static_cast<double>(value.fltVal));
                break;
            case VT_R8:
                std::snprintf(text, sizeof(text), "%g", value.dblVal);
                break;
            case VT_BSTR:
                return value.bstrVal != nullptr ? ToUtf8(value.bstrVal) : String();
            default:
                return String();
            }
            return String(text);
        }

        String TypeNameOf(IModelObject* object)
        {
            ComPtr<IDebugHostType> type;
            if (FAILED(object->GetTypeInfo(&type)) || type == nullptr)
            {
                return String();
            }
            BSTR name = nullptr;
            if (FAILED(type->GetName(&name)) || name == nullptr)
            {
                return String();
            }
            String text = ToUtf8(name);
            SysFreeString(name);
            return text;
        }

        Bool TypeKindOf(IModelObject* object, TypeKind& kind)
        {
            ComPtr<IDebugHostType> type;
            return SUCCEEDED(object->GetTypeInfo(&type)) && type != nullptr && SUCCEEDED(type->GetTypeKind(&kind));
        }

        // 데이터 모델이 이 값을 글자로 보이는 그대로다(`dx` 가 찍는 값, natvis 의 `DisplayString` 포함).
        String DisplayOf(IModelObject* object)
        {
            ComPtr<IUnknown> unknown;
            ComPtr<IKeyStore> metadata;
            if (SUCCEEDED(object->GetConcept(__uuidof(IStringDisplayableConcept), &unknown, &metadata)) && unknown != nullptr)
            {
                ComPtr<IStringDisplayableConcept> displayable;
                BSTR text = nullptr;
                if (SUCCEEDED(unknown.As(&displayable)) && SUCCEEDED(displayable->ToDisplayString(object, metadata.Get(), &text)) && text != nullptr)
                {
                    String display = ToUtf8(text);
                    SysFreeString(text);
                    return display;
                }
            }
            VARIANT value;
            VariantInit(&value);
            if (SUCCEEDED(object->GetIntrinsicValue(&value)))
            {
                String display = FormatVariant(value);
                VariantClear(&value);
                if (false == display.empty())
                {
                    return display;
                }
            }
            return String();
        }
    }

    // DbgEng 이 찍는 글자를 받는다. `JBRO_DAP_LOG` 가 파일을 가리키면 거기에 적고, 아니면 버린다 - 표준 출력은 DAP 통로라 쓰면 안 된다.
    class SessionOutput final : public IDebugOutputCallbacksWide
    {
    public:
        SessionOutput()
        {
            wchar_t path[MAX_PATH * 2] = {};
            if (GetEnvironmentVariableW(L"JBRO_DAP_LOG", path, MAX_PATH * 2) > 0)
            {
                m_file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            }
        }

        ~SessionOutput()
        {
            if (m_file != INVALID_HANDLE_VALUE)
            {
                CloseHandle(m_file);
            }
        }

        SessionOutput(const SessionOutput&) = delete;
        SessionOutput& operator=(const SessionOutput&) = delete;

        void Write(const String& text)
        {
            if (m_file == INVALID_HANDLE_VALUE)
            {
                return;
            }
            DWORD written = 0;
            WriteFile(m_file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        }

        STDMETHOD(QueryInterface)(REFIID id, PVOID* object) override
        {
            *object = nullptr;
            if (IsEqualIID(id, __uuidof(IUnknown)) || IsEqualIID(id, __uuidof(IDebugOutputCallbacksWide)))
            {
                *object = static_cast<IDebugOutputCallbacksWide*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        STDMETHOD_(ULONG, AddRef)() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&m_references));
        }

        STDMETHOD_(ULONG, Release)() override
        {
            // 지우지 않는다 - `DebugSession` 이 `OwnerPtr` 로 든다.
            return static_cast<ULONG>(InterlockedDecrement(&m_references));
        }

        STDMETHOD(Output)(ULONG, PCWSTR text) override
        {
            Write(ToUtf8(text));
            return S_OK;
        }

    private:
        HANDLE m_file = INVALID_HANDLE_VALUE;
        LONG m_references = 1;
    };

    // DbgEng 이 부르는 이벤트 콜백이다. 받은 것을 세션에 넘긴다.
    class SessionEvents final : public DebugBaseEventCallbacksWide
    {
    public:
        explicit SessionEvents(DebugSession& session)
            : m_session(session)
        {
        }

        STDMETHOD_(ULONG, AddRef)() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&m_references));
        }

        STDMETHOD_(ULONG, Release)() override
        {
            // 지우지 않는다 - `DebugSession` 이 `OwnerPtr` 로 든다.
            return static_cast<ULONG>(InterlockedDecrement(&m_references));
        }

        STDMETHOD(GetInterestMask)(PULONG mask) override
        {
            *mask = DEBUG_EVENT_BREAKPOINT | DEBUG_EVENT_EXCEPTION | DEBUG_EVENT_LOAD_MODULE | DEBUG_EVENT_UNLOAD_MODULE
                | DEBUG_EVENT_EXIT_PROCESS;
            return S_OK;
        }

        STDMETHOD(Breakpoint)(PDEBUG_BREAKPOINT2 breakpoint) override
        {
            return m_session.OnBreakpoint(breakpoint);
        }

        STDMETHOD(Exception)(PEXCEPTION_RECORD64 record, ULONG firstChance) override
        {
            return m_session.OnException(*record, firstChance != 0);
        }

        STDMETHOD(LoadModule)(ULONG64, ULONG64 baseOffset, ULONG, PCWSTR, PCWSTR imageName, ULONG, ULONG) override
        {
            return m_session.OnLoadModule(imageName, baseOffset);
        }

        STDMETHOD(UnloadModule)(PCWSTR, ULONG64 baseOffset) override
        {
            m_session.OnUnloadModule(baseOffset);
            return DEBUG_STATUS_NO_CHANGE;
        }

        STDMETHOD(ExitProcess)(ULONG exitCode) override
        {
            m_session.OnExitProcess(exitCode);
            return DEBUG_STATUS_NO_CHANGE;
        }

    private:
        DebugSession& m_session;
        LONG m_references = 1;
    };

    DebugSession::DebugSession(DapChannel& channel)
        : m_channel(channel)
    {
        m_scriptPrefixes.Add(String("GameScript"));
    }

    DebugSession::~DebugSession()
    {
        ReleaseHandles();
        // 콜백 객체(`m_events`·`m_output`)는 멤버라 COM 포인터보다 먼저 지워진다. 엔진이 지워진 콜백을 부르지 않게
        // 콜백을 떼고 COM 포인터를 여기서 차례대로 놓는다 - 그러지 않아 `IDebugClient` 를 놓다가 죽었다.
        if (m_client != nullptr)
        {
            m_client->SetEventCallbacksWide(nullptr);
            m_client->SetOutputCallbacksWide(nullptr);
            m_client->EndSession(DEBUG_END_ACTIVE_DETACH);
        }
        m_evaluator.Reset();
        m_host.Reset();
        m_dataModel.Reset();
        m_system.Reset();
        m_symbols.Reset();
        m_control.Reset();
        m_client.Reset();
        if (m_process != nullptr)
        {
            CloseHandle(m_process);
        }
        if (m_reader.joinable())
        {
            // 읽기 스레드는 표준 입력에서 막혀 있을 수 있다. 끝을 기다리지 않는다 - 프로세스가 끝나며 같이 끝난다.
            m_reader.detach();
        }
    }

    // ── 요청 큐 ───────────────────────────────────────────────────────────

    void DebugSession::ReadLoop()
    {
        while (true)
        {
            JsonValue message;
            if (false == m_channel.Read(message))
            {
                break;
            }
            if (message.GetString("type") != "request")
            {
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(m_queueLock);
                m_queue.push_back(std::move(message));
            }
            m_queueSignal.notify_one();
        }
        m_inputClosed = true;
        m_queueSignal.notify_one();
    }

    Bool DebugSession::PopRequest(JsonValue& request, Bool wait)
    {
        std::unique_lock<std::mutex> lock(m_queueLock);
        if (wait)
        {
            m_queueSignal.wait_for(lock, std::chrono::milliseconds(200), [this] { return false == m_queue.empty() || m_inputClosed.load(); });
        }
        if (m_queue.empty())
        {
            return false;
        }
        request = std::move(m_queue.front());
        m_queue.pop_front();
        return true;
    }

    Int32 DebugSession::Run()
    {
        m_reader = std::thread([this] { ReadLoop(); });
        while (false == m_quit)
        {
            if (m_state == TargetState::Running)
            {
                const HRESULT waited = m_control->WaitForEvent(0, 100);
                if (waited == S_OK)
                {
                    HandleBreak();
                    continue;
                }
                if (waited != S_FALSE)
                {
                    // 기다릴 대상이 없다 - 대상이 끝났다.
                    FinishTarget(m_exitCode);
                    continue;
                }
                // 도는 동안 온 요청은 대상을 세운 뒤에 처리한다. `pause` 는 사용자에게 알리는 멈춤이다.
                JsonValue request;
                Bool needBreak = false;
                while (PopRequest(request, false))
                {
                    const String command = request.GetString("command");
                    if (command == "pause")
                    {
                        m_pauseRequested = true;
                        m_channel.SendResponse(request, true, JsonValue());
                        needBreak = true;
                        continue;
                    }
                    if (command == "continue")
                    {
                        JsonValue body = JsonValue::MakeObject();
                        body.Set("allThreadsContinued", Bool(true));
                        m_channel.SendResponse(request, true, std::move(body));
                        continue;
                    }
                    m_deferred.Add(std::move(request));
                    needBreak = true;
                }
                if (m_inputClosed && m_deferred.IsEmpty())
                {
                    // 클라이언트가 사라졌다. 떼어 내고 끝낸다 - 에디터를 함께 죽이지 않는다.
                    JsonValue disconnect = JsonValue::MakeObject();
                    disconnect.Set("type", "request").Set("command", "disconnect").Set("seq", Int64(0));
                    m_deferred.Add(std::move(disconnect));
                    needBreak = true;
                }
                // 한 줄씩 가는 중이면 걸음이 끝나 멈출 때까지 미룬다(몰래 세우면 걸음을 잃는다). 일시정지만 바로 세운다.
                if (needBreak && (false == m_stepping || m_pauseRequested))
                {
                    RequestBreakIn();
                }
                continue;
            }
            JsonValue request;
            if (PopRequest(request, true))
            {
                Dispatch(request);
                continue;
            }
            if (m_inputClosed)
            {
                if (m_state == TargetState::Stopped)
                {
                    Detach();
                }
                m_quit = true;
            }
        }
        return m_exitCode;
    }

    void DebugSession::Dispatch(const JsonValue& request)
    {
        const String command = request.GetString("command");
        if (command == "initialize")
        {
            OnInitialize(request);
        }
        else if (command == "attach")
        {
            OnAttach(request);
        }
        else if (command == "setBreakpoints")
        {
            OnSetBreakpoints(request);
        }
        else if (command == "setExceptionBreakpoints")
        {
            JsonValue body = JsonValue::MakeObject();
            body.Set("breakpoints", JsonValue::MakeArray());
            m_channel.SendResponse(request, true, std::move(body));
        }
        else if (command == "configurationDone")
        {
            OnConfigurationDone(request);
        }
        else if (command == "threads")
        {
            OnThreads(request);
        }
        else if (command == "stackTrace")
        {
            OnStackTrace(request);
        }
        else if (command == "scopes")
        {
            OnScopes(request);
        }
        else if (command == "variables")
        {
            OnVariables(request);
        }
        else if (command == "evaluate")
        {
            OnEvaluate(request);
        }
        else if (command == "continue")
        {
            OnResume(request, ResumeKind::Go);
        }
        else if (command == "next")
        {
            OnResume(request, ResumeKind::StepOver);
        }
        else if (command == "stepIn")
        {
            OnResume(request, ResumeKind::StepInto);
        }
        else if (command == "stepOut")
        {
            OnResume(request, ResumeKind::StepOut);
        }
        else if (command == "pause")
        {
            // 이미 서 있다.
            m_channel.SendResponse(request, true, JsonValue());
        }
        else if (command == "disconnect" || command == "terminate")
        {
            OnDisconnect(request);
        }
        else
        {
            m_channel.SendResponse(request, false, JsonValue(), "unsupported request");
        }
    }

    // ── 요청 ──────────────────────────────────────────────────────────────

    void DebugSession::OnInitialize(const JsonValue& request)
    {
        JsonValue body = JsonValue::MakeObject();
        body.Set("supportsConfigurationDoneRequest", Bool(true));
        body.Set("supportsEvaluateForHovers", Bool(true));
        body.Set("supportTerminateDebuggee", Bool(false));
        body.Set("exceptionBreakpointFilters", JsonValue::MakeArray());
        m_channel.SendResponse(request, true, std::move(body));
    }

    void DebugSession::OnAttach(const JsonValue& request)
    {
        if (m_state != TargetState::None)
        {
            m_channel.SendResponse(request, false, JsonValue(), "already attached");
            return;
        }
        const JsonValue* arguments = request.Find("arguments");
        const JsonValue empty = JsonValue::MakeObject();
        if (arguments == nullptr)
        {
            arguments = &empty;
        }
        if (const JsonValue* prefixes = arguments->Find("scriptModules"); prefixes != nullptr && prefixes->GetSize() > 0)
        {
            m_scriptPrefixes.Clear();
            for (std::size_t index = 0; index < prefixes->GetSize(); ++index)
            {
                m_scriptPrefixes.Add(prefixes->At(index).AsString());
            }
        }
        ULONG processId = static_cast<ULONG>(arguments->GetInt64("processId").Value);
        if (processId == 0)
        {
            processId = FindProcessByName(arguments->GetString("processName", "JBroEditorHost.exe"));
        }
        if (processId == 0)
        {
            m_channel.SendResponse(request, false, JsonValue(), "the editor is not running");
            return;
        }
        // 심볼 서버(인터넷)로 가지 않는다. 스크립트 DLL 은 자기 PDB 의 절대 경로를 품고 있어 그것으로 충분하다.
        // 다만 기호 경로가 비면 DbgEng 은 심볼을 아예 읽지 않는다("Invalid") - 기본은 어댑터 폴더이고, 스크립트 모듈이 실리면 그 폴더를 더한다.
        String symbolPath = arguments->GetString("symbolPath", "");
        if (symbolPath.empty())
        {
            symbolPath = AdapterFolder();
        }
        SetEnvironmentVariableW(L"_NT_SYMBOL_PATH", ToWide(symbolPath).c_str());
        SetEnvironmentVariableW(L"_NT_ALT_SYMBOL_PATH", nullptr);

        if (FAILED(DebugCreate(__uuidof(IDebugClient5), reinterpret_cast<void**>(m_client.GetAddressOf())))
            || FAILED(m_client.As(&m_control)) || FAILED(m_client.As(&m_symbols)) || FAILED(m_client.As(&m_system)))
        {
            m_channel.SendResponse(request, false, JsonValue(), "the debugger engine could not start");
            return;
        }
        m_events = MakeOwnerPtr<SessionEvents>(*this);
        m_client->SetEventCallbacksWide(m_events.Get());
        m_output = MakeOwnerPtr<SessionOutput>();
        m_client->SetOutputCallbacksWide(m_output.Get());
        // 라이브러리로 쓰면 첫 멈춤이 꺼져 있다(cdb 는 켠다). 켜지 않으면 붙어도 멈추지 않는다.
        m_control->AddEngineOptions(DEBUG_ENGOPT_INITIAL_BREAK);
        m_symbols->AddSymbolOptions(SYMOPT_LOAD_LINES);
        // 붙을 때도 엔진이 대상에 끼어들기 스레드를 만든다. 그 int 3 은 첫 멈춤 뒤에 올 수도 있어, 우리가 보낸 끼어들기로 센다.
        m_pendingBreakIns = 1;
        if (FAILED(m_client->AttachProcess(0, processId, DEBUG_ATTACH_DEFAULT)) || m_control->WaitForEvent(0, 30000) != S_OK)
        {
            // 다른 디버거가 이미 붙어 있으면 여기다. 엔진을 내려 다음 요청이 반쯤 붙은 세션을 쓰지 않게 한다.
            m_client->SetEventCallbacksWide(nullptr);
            m_client->EndSession(DEBUG_END_ACTIVE_DETACH);
            m_system.Reset();
            m_symbols.Reset();
            m_control.Reset();
            m_client.Reset();
            m_channel.SendResponse(request, false, JsonValue(), "could not attach to the editor (is another debugger attached?)");
            return;
        }
        m_processId = processId;
        m_process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
        // 어댑터가 떼지 못하고 끝나도(죽거나 클라이언트가 강제로 닫아도) 에디터는 산다. 기본은 디버거가 끝날 때 대상도 끝낸다 -
        // 어댑터가 죽자 에디터가 같이 죽는 것을 봤다. `DebugSetProcessKillOnExit` 는 DbgEng 의 디버그 객체에 닿지 않아 듣지 않았다.
        m_client->AddProcessOptions(DEBUG_PROCESS_DETACH_ON_EXIT);
        // 한 줄씩 가기는 소스 줄 단위다.
        m_control->SetCodeLevel(DEBUG_LEVEL_SOURCE);
        m_symbols->SetSymbolPathWide(ToWide(symbolPath).c_str());
        // JBro 값 타입을 값으로 보인다(`Float` 이 구조체가 아니라 수로). 주지 않으면 어댑터 옆의 `JBro.natvis` 다.
        String natvis = arguments->GetString("natvis", "");
        if (natvis.empty())
        {
            natvis = AdapterFolder() + "JBro.natvis";
        }
        const std::wstring load = L".nvload \"" + ToWide(natvis) + L"\"";
        m_control->ExecuteWide(DEBUG_OUTCTL_THIS_CLIENT, load.c_str(), DEBUG_EXECUTE_NOT_LOGGED);
        ComPtr<IHostDataModelAccess> access;
        if (SUCCEEDED(m_client.As(&access)) && SUCCEEDED(access->GetDataModel(&m_dataModel, &m_host)))
        {
            m_host.As(&m_evaluator);
        }
        // 이미 실린 스크립트 모듈이다. 붙기 전에 실렸으므로 실림 이벤트가 오지 않는다.
        ULONG loaded = 0;
        ULONG unloaded = 0;
        m_symbols->GetNumberModules(&loaded, &unloaded);
        for (ULONG index = 0; index < loaded; ++index)
        {
            ULONG64 base = 0;
            wchar_t image[MAX_PATH * 2] = {};
            if (SUCCEEDED(m_symbols->GetModuleByIndex(index, &base))
                && SUCCEEDED(m_symbols->GetModuleNameStringWide(DEBUG_MODNAME_IMAGE, index, 0, image, ULONG(MAX_PATH * 2), nullptr))
                && IsScriptImage(ToUtf8(image)))
            {
                AddScriptModule(base);
            }
        }
        m_loadedScriptBases.Clear();
        m_hitBreakIn = false;
        m_hitException = false;
        m_hitBreakpointId = DEBUG_ANY_ID;
        m_state = TargetState::Stopped;
        m_channel.SendResponse(request, true, JsonValue());
        JsonValue output = JsonValue::MakeObject();
        char text[96] = {};
        std::snprintf(text, sizeof(text), "Attached to process %lu (%zu script module(s) loaded)\n", processId, m_scriptModules.Size());
        output.Set("category", "console").Set("output", text);
        m_channel.SendEvent("output", std::move(output));
        m_channel.SendEvent("initialized", JsonValue());
    }

    void DebugSession::OnSetBreakpoints(const JsonValue& request)
    {
        const JsonValue* arguments = request.Find("arguments");
        const JsonValue* source = arguments != nullptr ? arguments->Find("source") : nullptr;
        if (source == nullptr || m_control == nullptr)
        {
            m_channel.SendResponse(request, false, JsonValue(), "no source or not attached");
            return;
        }
        const String path = source->GetString("path");
        // 이 파일의 지난 중단점을 모두 뗀다. 요청은 그 파일의 전체 목록이다.
        for (std::size_t index = m_breakpoints.Size(); index > 0; --index)
        {
            if (_stricmp(m_breakpoints[index - 1].path.c_str(), path.c_str()) == 0)
            {
                ClearEngineBreakpoints(m_breakpoints[index - 1]);
                m_breakpoints.RemoveAt(index - 1);
            }
        }
        JsonValue results = JsonValue::MakeArray();
        if (const JsonValue* requested = arguments->Find("breakpoints"); requested != nullptr)
        {
            for (std::size_t index = 0; index < requested->GetSize(); ++index)
            {
                SourceBreakpoint breakpoint;
                breakpoint.path = path;
                breakpoint.line = Int32(static_cast<std::int32_t>(requested->At(index).GetInt64("line").Value));
                breakpoint.id = m_nextBreakpointId;
                m_nextBreakpointId = m_nextBreakpointId + 1;
                for (const ScriptModule& module : m_scriptModules)
                {
                    PlaceInModule(breakpoint, module);
                }
                results.Add(DescribeBreakpoint(breakpoint));
                m_breakpoints.Add(std::move(breakpoint));
            }
        }
        JsonValue body = JsonValue::MakeObject();
        body.Set("breakpoints", std::move(results));
        m_channel.SendResponse(request, true, std::move(body));
    }

    void DebugSession::OnConfigurationDone(const JsonValue& request)
    {
        m_channel.SendResponse(request, true, JsonValue());
        if (m_state == TargetState::Stopped)
        {
            // 붙을 때의 첫 멈춤이다. 사용자에게 알리지 않고 다시 돌린다.
            Resume(ResumeKind::Go);
        }
    }

    void DebugSession::OnThreads(const JsonValue& request)
    {
        JsonValue threads = JsonValue::MakeArray();
        ULONG count = 0;
        if (m_system != nullptr && SUCCEEDED(m_system->GetNumberThreads(&count)) && count > 0)
        {
            Array<ULONG> engineIds;
            Array<ULONG> systemIds;
            engineIds.Resize(count);
            systemIds.Resize(count);
            if (SUCCEEDED(m_system->GetThreadIdsByIndex(0, count, engineIds.Data(), systemIds.Data())))
            {
                for (ULONG index = 0; index < count; ++index)
                {
                    char name[48] = {};
                    if (index == 0)
                    {
                        std::snprintf(name, sizeof(name), "Main Thread (%lu)", systemIds[index]);
                    }
                    else
                    {
                        std::snprintf(name, sizeof(name), "Thread %lu", systemIds[index]);
                    }
                    JsonValue thread = JsonValue::MakeObject();
                    thread.Set("id", Int64(systemIds[index])).Set("name", name);
                    threads.Add(std::move(thread));
                }
            }
        }
        JsonValue body = JsonValue::MakeObject();
        body.Set("threads", std::move(threads));
        m_channel.SendResponse(request, true, std::move(body));
    }

    Bool DebugSession::SelectThread(Int64 systemId)
    {
        ULONG engineId = 0;
        return m_system != nullptr && SUCCEEDED(m_system->GetThreadIdBySystemId(static_cast<ULONG>(systemId.Value), &engineId))
            && SUCCEEDED(m_system->SetCurrentThreadId(engineId));
    }

    Bool DebugSession::SelectFrame(Int64 frameId)
    {
        const Int64 thread = frameId / FramesPerThread;
        const Int64 frame = frameId % FramesPerThread;
        return SelectThread(thread) && SUCCEEDED(m_symbols->SetScopeFrameByIndex(static_cast<ULONG>(frame.Value)));
    }

    void DebugSession::OnStackTrace(const JsonValue& request)
    {
        const JsonValue* arguments = request.Find("arguments");
        const Int64 threadId = arguments != nullptr ? arguments->GetInt64("threadId") : Int64(0);
        if (m_state != TargetState::Stopped || false == SelectThread(threadId))
        {
            m_channel.SendResponse(request, false, JsonValue(), "the thread is not stopped");
            return;
        }
        Array<DEBUG_STACK_FRAME> frames;
        frames.Resize(MaxFrames);
        ULONG filled = 0;
        m_control->GetStackTrace(0, 0, 0, frames.Data(), MaxFrames, &filled);
        const Int64 start = arguments->GetInt64("startFrame", 0);
        Int64 levels = arguments->GetInt64("levels", 0);
        if (levels <= 0)
        {
            levels = Int64(static_cast<std::int64_t>(filled));
        }
        JsonValue stackFrames = JsonValue::MakeArray();
        for (ULONG index = static_cast<ULONG>(start.Value); index < filled && Int64(static_cast<std::int64_t>(index)) < start + levels; ++index)
        {
            const ULONG64 offset = frames[index].InstructionOffset;
            wchar_t name[1024] = {};
            ULONG64 displacement = 0;
            String frameName;
            if (SUCCEEDED(m_symbols->GetNameByOffsetWide(offset, name, 1024, nullptr, &displacement)))
            {
                frameName = ToUtf8(name);
            }
            else
            {
                char address[32] = {};
                std::snprintf(address, sizeof(address), "0x%llx", static_cast<unsigned long long>(offset));
                frameName = address;
            }
            JsonValue frame = JsonValue::MakeObject();
            frame.Set("id", threadId * FramesPerThread + Int64(static_cast<std::int64_t>(index)));
            frame.Set("name", std::move(frameName));
            ULONG line = 0;
            wchar_t file[1024] = {};
            if (SUCCEEDED(m_symbols->GetLineByOffsetWide(offset, &line, file, 1024, nullptr, nullptr)))
            {
                const String path = ToUtf8(file);
                JsonValue sourceValue = JsonValue::MakeObject();
                sourceValue.Set("name", FileNameOf(path)).Set("path", path);
                frame.Set("source", std::move(sourceValue));
                frame.Set("line", Int64(line)).Set("column", Int64(1));
            }
            else
            {
                frame.Set("line", Int64(0)).Set("column", Int64(0)).Set("presentationHint", "subtle");
            }
            stackFrames.Add(std::move(frame));
        }
        JsonValue body = JsonValue::MakeObject();
        body.Set("stackFrames", std::move(stackFrames)).Set("totalFrames", Int64(filled));
        m_channel.SendResponse(request, true, std::move(body));
    }

    void DebugSession::OnScopes(const JsonValue& request)
    {
        const JsonValue* arguments = request.Find("arguments");
        const Int64 frameId = arguments != nullptr ? arguments->GetInt64("frameId") : Int64(0);
        const Int64 thread = frameId / FramesPerThread;
        const Int64 frame = frameId % FramesPerThread;
        JsonValue scopes = JsonValue::MakeArray();
        const char* const names[] = {"Locals", "Parameters"};
        const char* const members[] = {"LocalVariables", "Parameters"};
        for (std::size_t index = 0; index < 2; ++index)
        {
            char expression[160] = {};
            std::snprintf(expression, sizeof(expression), "@$curprocess.Threads[0x%llx].Stack.Frames[%lld].%s",
                static_cast<unsigned long long>(thread.Value), static_cast<long long>(frame.Value), members[index]);
            VariableHandle handle;
            handle.expression = expression;
            JsonValue scope = JsonValue::MakeObject();
            scope.Set("name", names[index]).Set("variablesReference", AddHandle(std::move(handle))).Set("expensive", Bool(false));
            if (index == 0)
            {
                scope.Set("presentationHint", "locals");
            }
            else
            {
                scope.Set("presentationHint", "arguments");
            }
            scopes.Add(std::move(scope));
        }
        JsonValue body = JsonValue::MakeObject();
        body.Set("scopes", std::move(scopes));
        m_channel.SendResponse(request, true, std::move(body));
    }

    void DebugSession::OnVariables(const JsonValue& request)
    {
        const JsonValue* arguments = request.Find("arguments");
        const Int64 reference = arguments != nullptr ? arguments->GetInt64("variablesReference") : Int64(0);
        JsonValue variables = JsonValue::MakeArray();
        if (m_state == TargetState::Stopped && reference > 0 && static_cast<std::size_t>(reference.Value) <= m_handles.Size())
        {
            VariableHandle& handle = m_handles[static_cast<std::size_t>(reference.Value) - 1];
            if (handle.object == nullptr && false == handle.expression.empty())
            {
                String error;
                Evaluate(handle.expression, handle.object, error);
            }
            if (handle.object != nullptr)
            {
                Array<Child> children;
                CollectChildren(handle.object.Get(), children);
                for (const Child& child : children)
                {
                    variables.Add(DescribeVariable(child.name, child.value.Get()));
                }
            }
        }
        JsonValue body = JsonValue::MakeObject();
        body.Set("variables", std::move(variables));
        m_channel.SendResponse(request, true, std::move(body));
    }

    void DebugSession::OnEvaluate(const JsonValue& request)
    {
        const JsonValue* arguments = request.Find("arguments");
        if (m_state != TargetState::Stopped || arguments == nullptr)
        {
            m_channel.SendResponse(request, false, JsonValue(), "not stopped");
            return;
        }
        if (const JsonValue* frameId = arguments->Find("frameId"); frameId != nullptr)
        {
            SelectFrame(frameId->AsInt64());
        }
        ComPtr<IModelObject> result;
        String error;
        if (false == Evaluate(arguments->GetString("expression"), result, error))
        {
            m_channel.SendResponse(request, false, JsonValue(), error.empty() ? "could not evaluate" : error.c_str());
            return;
        }
        JsonValue described = DescribeVariable(String(), result.Get());
        JsonValue body = JsonValue::MakeObject();
        body.Set("result", described.GetString("value"));
        body.Set("type", described.GetString("type"));
        body.Set("variablesReference", described.GetInt64("variablesReference"));
        m_channel.SendResponse(request, true, std::move(body));
    }

    void DebugSession::OnResume(const JsonValue& request, ResumeKind kind)
    {
        if (m_state != TargetState::Stopped)
        {
            m_channel.SendResponse(request, false, JsonValue(), "not stopped");
            return;
        }
        const JsonValue* arguments = request.Find("arguments");
        if (kind != ResumeKind::Go && arguments != nullptr)
        {
            SelectThread(arguments->GetInt64("threadId"));
        }
        JsonValue body = JsonValue::MakeObject();
        if (kind == ResumeKind::Go)
        {
            body.Set("allThreadsContinued", Bool(true));
        }
        m_channel.SendResponse(request, true, std::move(body));
        Resume(kind);
    }

    void DebugSession::OnDisconnect(const JsonValue& request)
    {
        if (m_state == TargetState::Stopped)
        {
            Detach();
        }
        m_quit = true;
        if (request.GetInt64("seq") > 0)
        {
            m_channel.SendResponse(request, true, JsonValue());
        }
    }

    // ── 멈춤과 다시 돌기 ──────────────────────────────────────────────────

    ULONG DebugSession::OnBreakpoint(IDebugBreakpoint2* breakpoint)
    {
        ULONG id = DEBUG_ANY_ID;
        breakpoint->GetId(&id);
        m_hitBreakpointId = id;
        return DEBUG_STATUS_BREAK;
    }

    ULONG DebugSession::OnException(const EXCEPTION_RECORD64& record, Bool firstChance)
    {
        // 우리가 보낸 끼어들기다(`DebugBreakProcess` 가 대상에 만든 스레드의 int 3).
        if (record.ExceptionCode == STATUS_BREAKPOINT && m_pendingBreakIns > 0)
        {
            m_pendingBreakIns = m_pendingBreakIns - 1;
            m_hitBreakIn = true;
            return DEBUG_STATUS_BREAK;
        }
        // 나머지는 엔진의 기본 거르기(첫 기회의 접근 위반은 멈춤 따위)에 맡기고 까닭만 적어 둔다.
        char text[160] = {};
        std::snprintf(text, sizeof(text), "Exception 0x%08lx at 0x%llx (%s chance)", static_cast<unsigned long>(record.ExceptionCode),
            static_cast<unsigned long long>(record.ExceptionAddress), firstChance ? "first" : "second");
        m_hitException = true;
        m_exceptionText = text;
        return DEBUG_STATUS_NO_CHANGE;
    }

    ULONG DebugSession::OnLoadModule(PCWSTR imageName, ULONG64 baseOffset)
    {
        if (imageName == nullptr || false == IsScriptImage(ToUtf8(imageName)))
        {
            return DEBUG_STATUS_NO_CHANGE;
        }
        m_loadedScriptBases.Add(baseOffset);
        // 새 스크립트 모듈에 중단점을 걸 때까지 세운다 - 그 사이 스크립트가 돌면 첫 중단점을 지나친다.
        // 한 줄씩 가는 중이면 세우지 않는다(세우면 가던 걸음을 잃는다). 걸음이 끝나 멈출 때 건다.
        return m_stepping ? DEBUG_STATUS_NO_CHANGE : DEBUG_STATUS_BREAK;
    }

    void DebugSession::OnUnloadModule(ULONG64 baseOffset)
    {
        for (std::size_t index = 0; index < m_scriptModules.Size(); ++index)
        {
            if (m_scriptModules[index].base == baseOffset)
            {
                m_scriptModules.RemoveAt(index);
                break;
            }
        }
    }

    void DebugSession::OnExitProcess(ULONG exitCode)
    {
        m_exited = true;
        m_exitCode = Int32(static_cast<std::int32_t>(exitCode));
    }

    void DebugSession::RequestBreakIn()
    {
        if (m_process == nullptr || m_pendingBreakIns > 0)
        {
            return;
        }
        if (DebugBreakProcess(m_process))
        {
            m_pendingBreakIns = m_pendingBreakIns + 1;
        }
    }

    void DebugSession::Resume(ResumeKind kind)
    {
        ReleaseHandles();
        m_resumeKind = kind;
        m_stepping = kind != ResumeKind::Go;
        if (kind == ResumeKind::StepOut)
        {
            // 지금 프레임의 돌아갈 자리에 이 스레드만의 한 번짜리 중단점을 건다.
            DEBUG_STACK_FRAME frames[2] = {};
            ULONG filled = 0;
            ULONG thread = 0;
            IDebugBreakpoint2* breakpoint = nullptr;
            if (SUCCEEDED(m_control->GetStackTrace(0, 0, 0, frames, 2, &filled)) && filled > 0
                && SUCCEEDED(m_control->AddBreakpoint2(DEBUG_BREAKPOINT_CODE, DEBUG_ANY_ID, &breakpoint)))
            {
                breakpoint->SetOffset(frames[0].ReturnOffset);
                if (SUCCEEDED(m_system->GetCurrentThreadId(&thread)))
                {
                    breakpoint->SetMatchThreadId(thread);
                }
                breakpoint->AddFlags(DEBUG_BREAKPOINT_ENABLED | DEBUG_BREAKPOINT_ONE_SHOT);
                breakpoint->GetId(&m_stepOutBreakpointId);
            }
            m_control->SetExecutionStatus(DEBUG_STATUS_GO);
        }
        else if (kind == ResumeKind::StepOver)
        {
            m_control->SetExecutionStatus(DEBUG_STATUS_STEP_OVER);
        }
        else if (kind == ResumeKind::StepInto)
        {
            m_control->SetExecutionStatus(DEBUG_STATUS_STEP_INTO);
        }
        else
        {
            m_control->SetExecutionStatus(DEBUG_STATUS_GO);
        }
        m_state = TargetState::Running;
    }

    void DebugSession::HandleBreak()
    {
        m_state = TargetState::Stopped;
        if (m_exited)
        {
            FinishTarget(m_exitCode);
            return;
        }
        // 새 스크립트 모듈에 중단점을 다시 건다.
        for (const ULONG64 base : m_loadedScriptBases)
        {
            AddScriptModule(base);
        }
        const Bool modulesLoaded = false == m_loadedScriptBases.IsEmpty();
        m_loadedScriptBases.Clear();

        const ULONG hit = m_hitBreakpointId;
        const Bool breakIn = m_hitBreakIn;
        const Bool exception = m_hitException;
        const String exceptionText = m_exceptionText;
        m_hitBreakpointId = DEBUG_ANY_ID;
        m_hitBreakIn = false;
        m_hitException = false;
        m_exceptionText.clear();

        JsonValue stopped = JsonValue::MakeObject();
        Bool visible = false;
        if (hit != DEBUG_ANY_ID && hit == m_stepOutBreakpointId)
        {
            m_stepOutBreakpointId = DEBUG_ANY_ID;
            stopped.Set("reason", "step");
            visible = true;
        }
        else if (hit != DEBUG_ANY_ID)
        {
            JsonValue ids = JsonValue::MakeArray();
            for (const SourceBreakpoint& breakpoint : m_breakpoints)
            {
                for (const ULONG engineId : breakpoint.engineIds)
                {
                    if (engineId == hit)
                    {
                        ids.Add(JsonValue(breakpoint.id));
                    }
                }
            }
            if (ids.GetSize() > 0)
            {
                stopped.Set("reason", "breakpoint").Set("hitBreakpointIds", std::move(ids));
                visible = true;
            }
        }
        else if (exception)
        {
            stopped.Set("reason", "exception").Set("text", exceptionText);
            visible = true;
        }
        else if (m_stepping && false == breakIn && false == modulesLoaded)
        {
            stopped.Set("reason", "step");
            visible = true;
        }
        if (false == visible && m_pauseRequested && breakIn)
        {
            stopped.Set("reason", "pause");
            visible = true;
        }

        if (visible)
        {
            m_pauseRequested = false;
            m_stepping = false;
            if (m_stepOutBreakpointId != DEBUG_ANY_ID)
            {
                // 다른 까닭으로 먼저 섰다. 나가기 중단점은 버린다.
                IDebugBreakpoint2* stepOut = nullptr;
                if (SUCCEEDED(m_control->GetBreakpointById2(m_stepOutBreakpointId, &stepOut)))
                {
                    m_control->RemoveBreakpoint2(stepOut);
                }
                m_stepOutBreakpointId = DEBUG_ANY_ID;
            }
            // 끼어들기로 섰으면 지금 스레드는 끼어들기 스레드다. 사용자가 볼 스레드는 첫 스레드(에디터의 메인)다.
            ULONG engineThread = 0;
            ULONG systemThread = 0;
            if (breakIn)
            {
                ULONG engineIds[1] = {};
                ULONG systemIds[1] = {};
                if (SUCCEEDED(m_system->GetThreadIdsByIndex(0, 1, engineIds, systemIds)))
                {
                    m_system->SetCurrentThreadId(engineIds[0]);
                }
            }
            m_system->GetCurrentThreadId(&engineThread);
            m_system->GetCurrentThreadSystemId(&systemThread);
            m_stoppedThread = systemThread;
            stopped.Set("threadId", Int64(systemThread)).Set("allThreadsStopped", Bool(true));
            m_channel.SendEvent("stopped", std::move(stopped));
        }

        // 도는 동안 와서 미룬 요청을 처리한다(멈춤을 알렸든 몰래 세웠든).
        Array<JsonValue> deferred = std::move(m_deferred);
        m_deferred.Clear();
        for (const JsonValue& request : deferred)
        {
            Dispatch(request);
            if (m_quit || m_state != TargetState::Stopped)
            {
                break;
            }
        }
        if (false == visible && false == m_quit && m_state == TargetState::Stopped)
        {
            // 사용자에게 알리지 않은 멈춤이다(스크립트 모듈이 실렸거나 미룬 요청을 처리했다). 다시 돌린다.
            Resume(ResumeKind::Go);
        }
    }

    void DebugSession::Detach()
    {
        for (SourceBreakpoint& breakpoint : m_breakpoints)
        {
            ClearEngineBreakpoints(breakpoint);
        }
        ReleaseHandles();
        if (m_client != nullptr)
        {
            m_client->DetachProcesses();
        }
        m_state = TargetState::Gone;
    }

    void DebugSession::FinishTarget(Int32 exitCode)
    {
        if (m_state == TargetState::Gone)
        {
            return;
        }
        m_state = TargetState::Gone;
        ReleaseHandles();
        JsonValue exited = JsonValue::MakeObject();
        exited.Set("exitCode", exitCode);
        m_channel.SendEvent("exited", std::move(exited));
        m_channel.SendEvent("terminated", JsonValue());
    }

    // ── 스크립트 모듈과 중단점 ────────────────────────────────────────────

    Bool DebugSession::IsScriptImage(const String& imagePath) const
    {
        const String file = FileNameOf(imagePath);
        for (const String& prefix : m_scriptPrefixes)
        {
            if (StartsWithNoCase(file, prefix))
            {
                return true;
            }
        }
        return false;
    }

    void DebugSession::AddScriptModule(ULONG64 base)
    {
        for (const ScriptModule& module : m_scriptModules)
        {
            if (module.base == base)
            {
                return;
            }
        }
        ULONG index = 0;
        wchar_t name[256] = {};
        if (FAILED(m_symbols->GetModuleByOffset(base, 0, &index, nullptr))
            || FAILED(m_symbols->GetModuleNameStringWide(DEBUG_MODNAME_MODULE, index, 0, name, 256, nullptr)))
        {
            return;
        }
        ScriptModule module;
        module.base = base;
        module.name = ToUtf8(name);
        // PDB 가 DLL 옆에 상대 이름으로만 있어도 찾게 그 폴더를 기호 경로에 더한다.
        wchar_t image[MAX_PATH * 2] = {};
        if (SUCCEEDED(m_symbols->GetModuleNameStringWide(DEBUG_MODNAME_IMAGE, index, 0, image, ULONG(MAX_PATH * 2), nullptr)))
        {
            const String imagePath = ToUtf8(image);
            const String folder = String(imagePath.substr(0, imagePath.find_last_of("\\/")));
            m_symbols->AppendSymbolPathWide(ToWide(folder).c_str());
        }
        // 스크립트 모듈의 심볼은 미뤄져 있다. 지금 읽어야 소스 줄로 중단점이 풀린다. `.reload /f` 는 모듈 이름이 아니라 이미지 이름을 받아
        // 복사본의 모듈 이름(`TargetScript_gen1`)으로는 실패했다 - `ld` 는 모듈 이름을 받는다.
        m_output->Write("script module: " + module.name + "\n");
        const std::wstring load = L"ld " + std::wstring(name);
        m_control->ExecuteWide(DEBUG_OUTCTL_THIS_CLIENT, load.c_str(), DEBUG_EXECUTE_NOT_LOGGED);
        // 지난 세대의 중단점은 모듈이 내려가며 갈 곳을 잃었다. 떼고 새 모듈에 다시 건다.
        for (SourceBreakpoint& breakpoint : m_breakpoints)
        {
            const Int32 before = breakpoint.resolvedLine;
            PlaceInModule(breakpoint, module);
            if (before != breakpoint.resolvedLine)
            {
                JsonValue body = JsonValue::MakeObject();
                body.Set("reason", "changed").Set("breakpoint", DescribeBreakpoint(breakpoint));
                m_channel.SendEvent("breakpoint", std::move(body));
            }
        }
        m_scriptModules.Add(std::move(module));
    }

    Bool DebugSession::PlaceInModule(SourceBreakpoint& breakpoint, const ScriptModule& module)
    {
        // 식은 `` `모듈!파일:줄` `` 이다. 전체 경로로 먼저, 안 되면 파일 이름으로 건다(PDB 에 적힌 경로가 대소문자·구분자에서 다를 수 있다).
        const String candidates[] = {breakpoint.path, FileNameOf(breakpoint.path)};
        for (const String& file : candidates)
        {
            IDebugBreakpoint2* engineBreakpoint = nullptr;
            if (FAILED(m_control->AddBreakpoint2(DEBUG_BREAKPOINT_CODE, DEBUG_ANY_ID, &engineBreakpoint)))
            {
                return false;
            }
            char line[16] = {};
            std::snprintf(line, sizeof(line), ":%d", static_cast<int>(breakpoint.line.Value));
            const String expression = "`" + module.name + "!" + file + line + "`";
            ULONG64 offset = 0;
            if (SUCCEEDED(engineBreakpoint->SetOffsetExpressionWide(ToWide(expression).c_str()))
                && SUCCEEDED(engineBreakpoint->GetOffset(&offset)) && offset != 0)
            {
                engineBreakpoint->AddFlags(DEBUG_BREAKPOINT_ENABLED);
                ULONG id = DEBUG_ANY_ID;
                engineBreakpoint->GetId(&id);
                breakpoint.engineIds.Add(id);
                ULONG resolved = 0;
                if (SUCCEEDED(m_symbols->GetLineByOffsetWide(offset, &resolved, nullptr, 0, nullptr, nullptr)))
                {
                    breakpoint.resolvedLine = Int32(static_cast<std::int32_t>(resolved));
                }
                else
                {
                    breakpoint.resolvedLine = breakpoint.line;
                }
                return true;
            }
            // 이 모듈에 그 줄이 없다. 미뤄진 중단점으로 남기지 않는다 - 모듈 이름은 다시 오지 않는다.
            m_output->Write("breakpoint expression did not resolve: " + expression + "\n");
            m_control->RemoveBreakpoint2(engineBreakpoint);
        }
        return false;
    }

    void DebugSession::ClearEngineBreakpoints(SourceBreakpoint& breakpoint)
    {
        for (const ULONG id : breakpoint.engineIds)
        {
            IDebugBreakpoint2* engineBreakpoint = nullptr;
            if (SUCCEEDED(m_control->GetBreakpointById2(id, &engineBreakpoint)))
            {
                m_control->RemoveBreakpoint2(engineBreakpoint);
            }
        }
        breakpoint.engineIds.Clear();
    }

    JsonValue DebugSession::DescribeBreakpoint(const SourceBreakpoint& breakpoint) const
    {
        JsonValue result = JsonValue::MakeObject();
        const Bool verified = breakpoint.resolvedLine > 0;
        result.Set("id", breakpoint.id).Set("verified", verified);
        result.Set("line", verified ? breakpoint.resolvedLine : breakpoint.line);
        JsonValue source = JsonValue::MakeObject();
        source.Set("name", FileNameOf(breakpoint.path)).Set("path", breakpoint.path);
        result.Set("source", std::move(source));
        if (false == verified)
        {
            result.Set("message", "No loaded script module has code on this line yet");
        }
        return result;
    }

    // ── 변수 ──────────────────────────────────────────────────────────────

    void DebugSession::ReleaseHandles()
    {
        m_handles.Clear();
    }

    Int32 DebugSession::AddHandle(VariableHandle handle)
    {
        m_handles.Add(std::move(handle));
        return Int32(static_cast<std::int32_t>(m_handles.Size()));
    }

    Bool DebugSession::Evaluate(const String& expression, ComPtr<IModelObject>& result, String& error)
    {
        if (m_evaluator == nullptr)
        {
            error = "the data model is not available";
            return false;
        }
        ComPtr<IDebugHostContext> context;
        m_host->GetCurrentContext(&context);
        const std::wstring wide = ToWide(expression);
        ComPtr<IModelObject> value;
        const HRESULT evaluated = m_evaluator->EvaluateExtendedExpression(context.Get(), wide.c_str(), nullptr, &value, nullptr);
        if (FAILED(evaluated) || value == nullptr)
        {
            // 실패하면 `value` 에 까닭을 담은 오류 객체가 올 수 있다.
            if (value != nullptr)
            {
                error = DisplayOf(value.Get());
            }
            return false;
        }
        result = value;
        return true;
    }

    void DebugSession::CollectChildren(IModelObject* object, Array<Child>& out)
    {
        ComPtr<IModelObject> target = object;
        TypeKind kind = TypeUDT;
        if (TypeKindOf(object, kind) && kind == TypePointer)
        {
            // 포인터는 가리키는 것을 편다(`dx` 와 같다).
            ComPtr<IModelObject> pointee;
            if (SUCCEEDED(object->Dereference(&pointee)) && pointee != nullptr)
            {
                target = pointee;
            }
        }
        // 키: natvis 의 펼침 항목, 지역 변수 목록 같은 합성 객체의 이름들.
        ComPtr<IKeyEnumerator> keys;
        if (SUCCEEDED(target->EnumerateKeyValues(&keys)) && keys != nullptr)
        {
            BSTR key = nullptr;
            ComPtr<IModelObject> value;
            while (out.Size() < MaxChildren && keys->GetNext(&key, &value, nullptr) == S_OK)
            {
                ModelObjectKind valueKind = ObjectNoValue;
                // 메서드(`ToDisplayString` 따위)는 값이 아니다.
                if (key != nullptr && value != nullptr && SUCCEEDED(value->GetKind(&valueKind)) && valueKind != ObjectMethod)
                {
                    out.Add(Child{ToUtf8(key), value});
                }
                SysFreeString(key);
                key = nullptr;
                value.Reset();
            }
        }
        // 차례로 도는 것: natvis 의 `ArrayItems`, 배열.
        ComPtr<IUnknown> unknown;
        ComPtr<IIterableConcept> iterable;
        if (SUCCEEDED(target->GetConcept(__uuidof(IIterableConcept), &unknown, nullptr)) && unknown != nullptr && SUCCEEDED(unknown.As(&iterable)))
        {
            ULONG64 dimensions = 0;
            iterable->GetDefaultIndexDimensionality(target.Get(), &dimensions);
            ComPtr<IModelIterator> iterator;
            if (dimensions <= 1 && SUCCEEDED(iterable->GetIterator(target.Get(), &iterator)) && iterator != nullptr)
            {
                std::size_t index = 0;
                ComPtr<IModelObject> item;
                IModelObject* indexers[1] = {};
                while (out.Size() < MaxChildren && iterator->GetNext(&item, dimensions, dimensions > 0 ? indexers : nullptr, nullptr) == S_OK)
                {
                    char name[32] = {};
                    std::snprintf(name, sizeof(name), "[%zu]", index);
                    out.Add(Child{String(name), item});
                    item.Reset();
                    if (indexers[0] != nullptr)
                    {
                        indexers[0]->Release();
                        indexers[0] = nullptr;
                    }
                    ++index;
                }
            }
        }
        if (false == out.IsEmpty())
        {
            return;
        }
        // 날것: 부모 클래스와 필드. 대상 메모리의 객체에만 묻는다 - 합성 객체(매개변수 목록 따위)에 물으면 dbgmodel 안에서 죽었다.
        ModelObjectKind targetKind = ObjectNoValue;
        if (FAILED(target->GetKind(&targetKind)) || (targetKind != ObjectTargetObject && targetKind != ObjectTargetObjectReference))
        {
            return;
        }
        const SymbolKind kinds[] = {SymbolBaseClass, SymbolField};
        for (const SymbolKind rawKind : kinds)
        {
            ComPtr<IRawEnumerator> raw;
            if (FAILED(target->EnumerateRawValues(rawKind, 0, &raw)) || raw == nullptr)
            {
                continue;
            }
            BSTR name = nullptr;
            SymbolKind found = Symbol;
            ComPtr<IModelObject> value;
            while (out.Size() < MaxChildren && raw->GetNext(&name, &found, &value) == S_OK)
            {
                if (value != nullptr)
                {
                    String childName = name != nullptr ? ToUtf8(name) : String();
                    if (rawKind == SymbolBaseClass)
                    {
                        childName = "[" + childName + "]";
                    }
                    out.Add(Child{std::move(childName), value});
                }
                SysFreeString(name);
                name = nullptr;
                value.Reset();
            }
        }
    }

    JsonValue DebugSession::DescribeVariable(const String& name, IModelObject* object)
    {
        JsonValue variable = JsonValue::MakeObject();
        variable.Set("name", name);
        String value = DisplayOf(object);
        const String type = TypeNameOf(object);
        ModelObjectKind objectKind = ObjectNoValue;
        object->GetKind(&objectKind);
        TypeKind typeKind = TypeUDT;
        const Bool typed = TypeKindOf(object, typeKind);
        Bool expandable = objectKind == ObjectSynthetic;
        if (typed && (typeKind == TypeUDT || typeKind == TypeArray))
        {
            expandable = true;
        }
        if (typed && typeKind == TypePointer)
        {
            VARIANT address;
            VariantInit(&address);
            if (SUCCEEDED(object->GetIntrinsicValue(&address)))
            {
                expandable = address.vt == VT_UI8 ? address.ullVal != 0 : true;
                VariantClear(&address);
            }
        }
        if (value.empty())
        {
            value = type.empty() ? String("{...}") : "{" + type + "}";
        }
        variable.Set("value", std::move(value)).Set("type", type);
        Int32 reference = 0;
        if (expandable)
        {
            VariableHandle handle;
            handle.object = object;
            reference = AddHandle(std::move(handle));
        }
        variable.Set("variablesReference", reference);
        return variable;
    }
}
