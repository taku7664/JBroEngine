#include "DebugSession.h"

#include "Text.h"

#include <DbgHelp.h>
#include <TlHelp32.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace JBro::DebugAdapter
{
    namespace
    {
        constexpr std::size_t MaxChildren = 1000;
        constexpr ULONG MaxFrames = 256;
        constexpr Int64 FramesPerThread = 1000;

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

    // DbgEng 이 찍는 글자를 받는다. `JBRO_DAP_LOG` 가 파일을 가리키면 거기에 적고, 아니면 버린다 - 표준 출력은 DAP 통로다.
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

    DebugSession::DebugSession(DapChannel& channel)
        : m_channel(channel)
    {
        m_scriptPrefixes.Add(String("GameScript"));
    }

    DebugSession::~DebugSession()
    {
        ReleaseHandles();
        if (m_pipe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_pipe);
        }
        if (m_client != nullptr)
        {
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
        if (m_reader.joinable())
        {
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
            m_queueSignal.wait_for(lock, std::chrono::milliseconds(20), [this] { return false == m_queue.empty() || m_inputClosed.load(); });
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
            // 에디터가 보낸 멈춤·재개·모듈·끝남을 먼저 본다. noninvasive 라 디버그 이벤트가 없다 - 모든 소식이 파이프로 온다.
            PumpPipe();
            if (m_quit)
            {
                break;
            }
            JsonValue request;
            // 붙기 전에는 요청을 기다리며 쉰다. 붙은 뒤에는 파이프를 자주 보려고 짧게만 기다린다.
            if (PopRequest(request, m_state == State::None))
            {
                Dispatch(request);
                continue;
            }
            if (m_inputClosed && m_queue.empty())
            {
                if (m_state == State::Running || m_state == State::Stopped)
                {
                    PipeSend("stop");
                }
                m_quit = true;
            }
        }
        return m_exitCode;
    }

    void DebugSession::Dispatch(const JsonValue& request)
    {
        const String command = request.GetString("command");
        if (command == "initialize") { OnInitialize(request); }
        else if (command == "attach") { OnAttach(request); }
        else if (command == "setBreakpoints") { OnSetBreakpoints(request); }
        else if (command == "setExceptionBreakpoints")
        {
            JsonValue body = JsonValue::MakeObject();
            body.Set("breakpoints", JsonValue::MakeArray());
            m_channel.SendResponse(request, true, std::move(body));
        }
        else if (command == "configurationDone") { OnConfigurationDone(request); }
        else if (command == "threads") { OnThreads(request); }
        else if (command == "stackTrace") { OnStackTrace(request); }
        else if (command == "scopes") { OnScopes(request); }
        else if (command == "variables") { OnVariables(request); }
        else if (command == "evaluate") { OnEvaluate(request); }
        else if (command == "continue") { OnResume(request, "continue"); }
        else if (command == "next") { OnResume(request, "step over"); }
        else if (command == "stepIn") { OnResume(request, "step into"); }
        else if (command == "stepOut") { OnResume(request, "step out"); }
        else if (command == "pause")
        {
            // 세우지 않고 붙으므로 어댑터가 일시정지를 걸지 못한다. 사용자가 에디터에서 멈춘다.
            m_channel.SendResponse(request, false, JsonValue(), "pause the script from the editor");
        }
        else if (command == "disconnect" || command == "terminate") { OnDisconnect(request); }
        else { m_channel.SendResponse(request, false, JsonValue(), "unsupported request"); }
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
        if (m_state != State::None)
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
        m_output = MakeOwnerPtr<SessionOutput>();
        m_client->SetOutputCallbacksWide(m_output.Get());
        m_symbols->AddSymbolOptions(SYMOPT_LOAD_LINES);
        m_control->SetCodeLevel(DEBUG_LEVEL_SOURCE);
        // **세우지 않고 붙는다**(D-330). noninvasive 는 메모리와 심볼만 읽고 대상 스레드를 멈추지 않는다 - 에디터가 계속 돈다.
        if (FAILED(m_client->AttachProcess(0, processId, DEBUG_ATTACH_NONINVASIVE | DEBUG_ATTACH_NONINVASIVE_NO_SUSPEND))
            || m_control->WaitForEvent(0, 30000) != S_OK)
        {
            m_client->SetOutputCallbacksWide(nullptr);
            m_client->EndSession(DEBUG_END_ACTIVE_DETACH);
            m_system.Reset();
            m_symbols.Reset();
            m_control.Reset();
            m_client.Reset();
            m_channel.SendResponse(request, false, JsonValue(), "could not read the editor");
            return;
        }
        m_processId = processId;
        m_symbols->SetSymbolPathWide(ToWide(symbolPath).c_str());
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
        EnumerateScriptModules();

        if (false == ConnectEditorPipe(processId))
        {
            m_client->EndSession(DEBUG_END_ACTIVE_DETACH);
            m_client.Reset();
            m_channel.SendResponse(request, false, JsonValue(), "the editor's debug channel is not open");
            return;
        }
        m_state = State::Running;
        m_channel.SendResponse(request, true, JsonValue());
        JsonValue output = JsonValue::MakeObject();
        char text[96] = {};
        std::snprintf(text, sizeof(text), "Attached to process %lu without stopping it (%zu script module(s))\n", processId, m_scriptModules.Size());
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
        for (std::size_t index = m_breakpoints.Size(); index > 0; --index)
        {
            if (_stricmp(m_breakpoints[index - 1].path.c_str(), path.c_str()) == 0)
            {
                DisarmBreakpoint(m_breakpoints[index - 1]);
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
                ResolveAndArm(breakpoint);
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
    }

    void DebugSession::OnThreads(const JsonValue& request)
    {
        // 멈췄을 때만 스레드를 안다(에디터가 보낸 그 스레드 하나). 돌고 있으면 빈 목록이다.
        JsonValue threads = JsonValue::MakeArray();
        if (m_state == State::Stopped && m_stoppedThread != 0)
        {
            char name[48] = {};
            std::snprintf(name, sizeof(name), "Editor thread %lu", m_stoppedThread);
            JsonValue thread = JsonValue::MakeObject();
            thread.Set("id", Int64(m_stoppedThread)).Set("name", name);
            threads.Add(std::move(thread));
        }
        JsonValue body = JsonValue::MakeObject();
        body.Set("threads", std::move(threads));
        m_channel.SendResponse(request, true, std::move(body));
    }

    Bool DebugSession::SelectFrame(Int64 frameId)
    {
        const Int64 frame = frameId % FramesPerThread;
        return m_symbols != nullptr && SUCCEEDED(m_symbols->SetScopeFrameByIndex(static_cast<ULONG>(frame.Value)));
    }

    void DebugSession::OnStackTrace(const JsonValue& request)
    {
        const JsonValue* arguments = request.Find("arguments");
        const Int64 threadId = arguments != nullptr ? arguments->GetInt64("threadId") : Int64(0);
        if (m_state != State::Stopped)
        {
            m_channel.SendResponse(request, false, JsonValue(), "the script is not stopped");
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
                const String filePath = ToUtf8(file);
                JsonValue sourceValue = JsonValue::MakeObject();
                sourceValue.Set("name", FileNameOf(filePath)).Set("path", filePath);
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
        SelectFrame(frameId);
        JsonValue scopes = JsonValue::MakeArray();
        // `.cxr` 뒤에는 데이터 모델의 `@$curthread` 경로가 비어 있다(실측). 지금 프레임의 지역·매개변수는 `@$curframe` 으로 읽는다.
        const char* const names[] = {"Locals", "Parameters"};
        const char* const members[] = {"LocalVariables", "Parameters"};
        for (std::size_t index = 0; index < 2; ++index)
        {
            VariableHandle handle;
            handle.expression = String("@$curframe.") + members[index];
            JsonValue scope = JsonValue::MakeObject();
            scope.Set("name", names[index]).Set("variablesReference", AddHandle(std::move(handle))).Set("expensive", Bool(false));
            scope.Set("presentationHint", index == 0 ? "locals" : "arguments");
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
        if (m_state == State::Stopped && reference > 0 && static_cast<std::size_t>(reference.Value) <= m_handles.Size())
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
        if (m_state != State::Stopped || arguments == nullptr)
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

    void DebugSession::OnResume(const JsonValue& request, const char* pipeCommand)
    {
        if (m_state != State::Stopped)
        {
            m_channel.SendResponse(request, false, JsonValue(), "not stopped");
            return;
        }
        JsonValue body = JsonValue::MakeObject();
        if (std::strcmp(pipeCommand, "continue") == 0)
        {
            body.Set("allThreadsContinued", Bool(true));
        }
        m_channel.SendResponse(request, true, std::move(body));
        ReleaseHandles();
        // 걸음(step)은 엔진이 줄까지 판단한다(D-330 §3.6). 어댑터는 명령만 넘긴다.
        PipeSend(pipeCommand);
        m_state = State::Running;
        m_stoppedContext = 0;
    }

    void DebugSession::OnDisconnect(const JsonValue& request)
    {
        if (m_state == State::Running || m_state == State::Stopped)
        {
            for (SourceBreakpoint& breakpoint : m_breakpoints)
            {
                DisarmBreakpoint(breakpoint);
            }
            if (m_state == State::Stopped)
            {
                PipeSend("continue");
            }
            if (m_client != nullptr)
            {
                m_client->DetachProcesses();
            }
            m_state = State::Gone;
        }
        m_quit = true;
        if (request.GetInt64("seq") > 0)
        {
            m_channel.SendResponse(request, true, JsonValue());
        }
    }

    // ── 에디터와의 파이프 ──────────────────────────────────────────────────

    Bool DebugSession::ConnectEditorPipe(ULONG processId)
    {
        wchar_t name[64] = {};
        std::swprintf(name, 64, L"\\\\.\\pipe\\jbro-debug-%lu", processId);
        for (int tries = 0; tries < 100; ++tries)
        {
            m_pipe = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (m_pipe != INVALID_HANDLE_VALUE)
            {
                DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
                SetNamedPipeHandleState(m_pipe, &mode, nullptr, nullptr);
                return true;
            }
            Sleep(10);
        }
        return false;
    }

    void DebugSession::PipeSend(const String& line)
    {
        if (m_pipe == INVALID_HANDLE_VALUE)
        {
            return;
        }
        String packet = line;
        packet.push_back('\n');
        DWORD written = 0;
        WriteFile(m_pipe, packet.data(), static_cast<DWORD>(packet.size()), &written, nullptr);
    }

    void DebugSession::PumpPipe()
    {
        if (m_pipe == INVALID_HANDLE_VALUE)
        {
            return;
        }
        char chunk[4096];
        DWORD read = 0;
        while (ReadFile(m_pipe, chunk, sizeof(chunk), &read, nullptr) && read > 0)
        {
            m_pipeInbox.append(chunk, read);
            if (read < sizeof(chunk))
            {
                break;
            }
        }
        std::size_t newline = m_pipeInbox.find('\n');
        while (newline != String::npos)
        {
            String line = m_pipeInbox.Substr(0, newline);
            if (false == line.empty() && line[line.size() - 1] == '\r')
            {
                line.resize(line.size() - 1);
            }
            m_pipeInbox.erase(0, newline + 1);
            if (false == line.empty())
            {
                OnEditorLine(line);
            }
            newline = m_pipeInbox.find('\n');
        }
    }

    void DebugSession::OnEditorLine(const String& line)
    {
        const std::size_t space = line.find(' ');
        const String command = space == String::npos ? line : line.Substr(0, space);
        const String rest = space == String::npos ? String() : line.Substr(space + 1);
        if (command == "stopped")
        {
            unsigned long long address = 0;
            unsigned long long context = 0;
            unsigned long thread = 0;
            if (::sscanf_s(rest.c_str(), "%llx %llx %lu", &address, &context, &thread) == 3)
            {
                OnStoppedFromEditor(address, context, thread);
            }
        }
        else if (command == "resumed")
        {
            // 에디터가 이어 갔다. 이미 `continue` 뒤라면 상태는 Running 이다.
        }
        else if (command == "module")
        {
            const std::size_t gap = rest.find(' ');
            if (gap != String::npos)
            {
                OnModuleFromEditor(rest.Substr(0, gap) == "1", rest.Substr(gap + 1));
            }
        }
        else if (command == "exited")
        {
            FinishTarget(0);
        }
        else if (command == "bp")
        {
            // setbp 답이다. `bp <주소> <0|1>`. 걸렸는지만 표식한다.
            unsigned long long address = 0;
            int ok = 0;
            if (::sscanf_s(rest.c_str(), "%llx %d", &address, &ok) == 2)
            {
                for (SourceBreakpoint& breakpoint : m_breakpoints)
                {
                    if (breakpoint.address == ULONG64(address))
                    {
                        breakpoint.armed = ok != 0;
                    }
                }
            }
        }
    }

    void DebugSession::OnStoppedFromEditor(ULONG64 address, ULONG64 contextAddress, ULONG threadId)
    {
        m_stoppedContext = contextAddress;
        m_stoppedThread = threadId;
        m_state = State::Stopped;
        ReleaseHandles();
        // **멈춘 스레드의 문맥으로 바꾼다**(D-330). 이 뒤의 스택·변수 읽기가 이 문맥을 따른다.
        char command[48] = {};
        std::snprintf(command, sizeof(command), ".cxr 0x%llx", static_cast<unsigned long long>(contextAddress));
        m_control->ExecuteWide(DEBUG_OUTCTL_THIS_CLIENT, ToWide(command).c_str(), DEBUG_EXECUTE_NOT_LOGGED);

        JsonValue stopped = JsonValue::MakeObject();
        JsonValue ids = JsonValue::MakeArray();
        for (const SourceBreakpoint& breakpoint : m_breakpoints)
        {
            if (breakpoint.address == address && breakpoint.address != 0)
            {
                ids.Add(JsonValue(breakpoint.id));
            }
        }
        stopped.Set("reason", ids.GetSize() > 0 ? "breakpoint" : "step");
        if (ids.GetSize() > 0)
        {
            stopped.Set("hitBreakpointIds", std::move(ids));
        }
        stopped.Set("threadId", Int64(threadId)).Set("allThreadsStopped", Bool(true));
        m_channel.SendEvent("stopped", std::move(stopped));
    }

    void DebugSession::OnModuleFromEditor(Bool loaded, const String& path)
    {
        if (false == IsScriptImage(path))
        {
            return;
        }
        // 새 복사본이 실렸다. DbgEng 의 모듈 목록을 새로 읽게 한 뒤(noninvasive 는 자동으로 갱신되지 않는다) 중단점의 주소를 다시 푼다.
        m_control->ExecuteWide(DEBUG_OUTCTL_THIS_CLIENT, L".reload", DEBUG_EXECUTE_NOT_LOGGED);
        EnumerateScriptModules();
        for (SourceBreakpoint& breakpoint : m_breakpoints)
        {
            const Int32 before = breakpoint.resolvedLine;
            const ULONG64 beforeAddress = breakpoint.address;
            ResolveAndArm(breakpoint);
            if (before != breakpoint.resolvedLine || beforeAddress != breakpoint.address)
            {
                JsonValue body = JsonValue::MakeObject();
                body.Set("reason", "changed").Set("breakpoint", DescribeBreakpoint(breakpoint));
                m_channel.SendEvent("breakpoint", std::move(body));
            }
        }
        (void)loaded;
    }

    // ── 심볼과 중단점 ──────────────────────────────────────────────────────

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

    void DebugSession::EnumerateScriptModules()
    {
        m_scriptModules.Clear();
        ULONG loaded = 0;
        ULONG unloaded = 0;
        if (FAILED(m_symbols->GetNumberModules(&loaded, &unloaded)))
        {
            return;
        }
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
    }

    void DebugSession::AddScriptModule(ULONG64 base)
    {
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
        wchar_t image[MAX_PATH * 2] = {};
        if (SUCCEEDED(m_symbols->GetModuleNameStringWide(DEBUG_MODNAME_IMAGE, index, 0, image, ULONG(MAX_PATH * 2), nullptr)))
        {
            const String imagePath = ToUtf8(image);
            const String folder = String(imagePath.substr(0, imagePath.find_last_of("\\/")));
            m_symbols->AppendSymbolPathWide(ToWide(folder).c_str());
        }
        const std::wstring load = L"ld " + std::wstring(name);
        m_control->ExecuteWide(DEBUG_OUTCTL_THIS_CLIENT, load.c_str(), DEBUG_EXECUTE_NOT_LOGGED);
        m_scriptModules.Add(std::move(module));
    }

    Bool DebugSession::ResolveAndArm(SourceBreakpoint& breakpoint)
    {
        // 지난 복사본에 걸어 둔 것을 뗀다.
        DisarmBreakpoint(breakpoint);
        const String candidates[] = {breakpoint.path, FileNameOf(breakpoint.path)};
        for (const ScriptModule& module : m_scriptModules)
        {
            for (const String& file : candidates)
            {
                char line[16] = {};
                std::snprintf(line, sizeof(line), ":%d", static_cast<int>(breakpoint.line.Value));
                const std::wstring expression = ToWide("`" + module.name + "!" + file + line + "`");
                DEBUG_VALUE value = {};
                if (SUCCEEDED(m_control->EvaluateWide(expression.c_str(), DEBUG_VALUE_INT64, &value, nullptr)) && value.I64 != 0)
                {
                    breakpoint.address = static_cast<ULONG64>(value.I64);
                    ULONG resolved = 0;
                    if (SUCCEEDED(m_symbols->GetLineByOffsetWide(breakpoint.address, &resolved, nullptr, 0, nullptr, nullptr)))
                    {
                        breakpoint.resolvedLine = Int32(static_cast<std::int32_t>(resolved));
                    }
                    else
                    {
                        breakpoint.resolvedLine = breakpoint.line;
                    }
                    // 에디터에게 그 자리에 int 3 을 넣어 달라 한다. 답(`bp`)이 오면 `armed` 가 참이 된다.
                    char command[48] = {};
                    std::snprintf(command, sizeof(command), "setbp %llx", static_cast<unsigned long long>(breakpoint.address));
                    PipeSend(command);
                    return true;
                }
            }
        }
        return false;
    }

    void DebugSession::DisarmBreakpoint(SourceBreakpoint& breakpoint)
    {
        if (breakpoint.address != 0)
        {
            char command[48] = {};
            std::snprintf(command, sizeof(command), "clearbp %llx", static_cast<unsigned long long>(breakpoint.address));
            PipeSend(command);
        }
        breakpoint.address = 0;
        breakpoint.armed = false;
    }

    JsonValue DebugSession::DescribeBreakpoint(const SourceBreakpoint& breakpoint) const
    {
        JsonValue result = JsonValue::MakeObject();
        const Bool verified = breakpoint.resolvedLine > 0 && breakpoint.address != 0;
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

    void DebugSession::FinishTarget(Int32 exitCode)
    {
        if (m_state == State::Gone)
        {
            return;
        }
        m_state = State::Gone;
        ReleaseHandles();
        JsonValue exited = JsonValue::MakeObject();
        exited.Set("exitCode", exitCode);
        m_channel.SendEvent("exited", std::move(exited));
        m_channel.SendEvent("terminated", JsonValue());
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
            ComPtr<IModelObject> pointee;
            if (SUCCEEDED(object->Dereference(&pointee)) && pointee != nullptr)
            {
                target = pointee;
            }
        }
        ComPtr<IKeyEnumerator> keys;
        if (SUCCEEDED(target->EnumerateKeyValues(&keys)) && keys != nullptr)
        {
            BSTR key = nullptr;
            ComPtr<IModelObject> value;
            while (out.Size() < MaxChildren && keys->GetNext(&key, &value, nullptr) == S_OK)
            {
                ModelObjectKind valueKind = ObjectNoValue;
                if (key != nullptr && value != nullptr && SUCCEEDED(value->GetKind(&valueKind)) && valueKind != ObjectMethod)
                {
                    out.Add(Child{ToUtf8(key), value});
                }
                SysFreeString(key);
                key = nullptr;
                value.Reset();
            }
        }
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
