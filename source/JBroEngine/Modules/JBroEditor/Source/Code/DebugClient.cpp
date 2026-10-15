#include <JBro/Editor/Code/DebugClient.h>
#include <JBro/Editor/EditorPaths.h>
#include <JBro/Platform/Platform.h>
#include <nlohmann/json.hpp>
#include <charconv>

namespace JBro::Code
{
    using Json = nlohmann::json;
    namespace
    {
        constexpr std::size_t MaxBody = 4 * 1024 * 1024;
        constexpr std::size_t MaxHeader = 8192;
        constexpr std::size_t MaxRequests = 1024;
        String Packet(const Json& value)
        {
            const String body = value.dump();
            return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        }
    }
    struct DebugClient::State
    {
        struct Document
        {
            String path;
            Array<Breakpoint> breakpoints;
            Array<Int32> ids;
            UInt64 generation = 0;
            Bool dirty = true;
        };
        struct Request
        {
            Int32 seq;
            String command, path;
            UInt64 generation = 0;
        };
        IPlatform* platform;
        OwnerPtr<IProcessChannel> channel;
        Array<Document> documents;
        Array<Request> requests;
        String incoming, error;
        DebugState state = DebugState::Idle;
        UInt32 processId = 0, threadId = 0;
        Int32 nextSeq = 1;
        UInt64 revision = 0, configurationGeneration = 0;
        Bool attached = false, initialized = false, configure = false;
        Float startupAge = 0.0f, cleanupAge = 0.0f;
        explicit State(IPlatform& value) : platform(&value) {}
        Document* Find(const String& path)
        {
            const String normalized = EditorPaths::Normalize(path);
            for (auto& document : documents)
            {
                if (document.path == normalized)
                {
                    return &document;
                }
            }
            return nullptr;
        }
        void Invalidate()
        {
            for (auto& document : documents)
            {
                document.ids.Clear();
                document.dirty = true;
                for (auto& breakpoint : document.breakpoints)
                {
                    breakpoint.verified = false;
                    breakpoint.actualLine = 0;
                    breakpoint.message.clear();
                }
            }
            ++revision;
        }
        void CloseChannel()
        {
            channel = nullptr;
            incoming.clear();
            requests.Clear();
            attached = initialized = false;
            if (state != DebugState::Failed)
            {
                state = DebugState::Idle;
            }
            ++revision;
        }
        void Disconnect(Bool failed)
        {
            state = failed ? DebugState::Failed : DebugState::Stopping;
            cleanupAge = 0.0f;
            requests.Clear();
            Invalidate();
            if (channel.Get() == nullptr)
            {
                CloseChannel();
                return;
            }
            const Int32 seq = nextSeq++;
            requests.Add({seq, "disconnect", {}, 0});
            const String packet = Packet({{"seq", seq.Get()}, {"type", "request"}, {"command", "disconnect"},
                {"arguments", {{"terminateDebuggee", false}}}});
            if (!channel->Write(packet.View()))
            {
                CloseChannel();
            }
        }
        void Fail(const String& message)
        {
            if (state == DebugState::Failed)
            {
                return;
            }
            error = message;
            Disconnect(true);
        }
        Bool Send(const char* command, Json arguments = Json::object(), const String& path = {}, UInt64 generation = 0)
        {
            if (requests.Size() >= MaxRequests)
            {
                Fail("The debugger request queue is full");
                return false;
            }
            const Int32 seq = nextSeq++;
            const String packet = Packet({{"seq", seq.Get()}, {"type", "request"}, {"command", command}, {"arguments", std::move(arguments)}});
            if (channel.Get() == nullptr || !channel->Write(packet.View()))
            {
                Fail("The debugger input channel is unavailable");
                return false;
            }
            requests.Add({seq, command, path, generation});
            return true;
        }
        void Flush()
        {
            if (!attached || !initialized || (state != DebugState::Starting && state != DebugState::Configuring
                && state != DebugState::Ready && state != DebugState::Stopped))
            {
                return;
            }
            for (auto& document : documents)
            {
                if (document.dirty)
                {
                    Json entries = Json::array();
                    for (const auto& breakpoint : document.breakpoints)
                    {
                        entries.push_back({{"line", breakpoint.requestedLine.Get()}});
                    }
                    if (!Send("setBreakpoints", {{"source", {{"path", document.path.Std()}}}, {"breakpoints", std::move(entries)},
                        {"sourceModified", false}}, document.path, document.generation))
                    {
                        return;
                    }
                    document.dirty = false;
                }
            }
            if (configure && state != DebugState::Ready && state != DebugState::Stopped)
            {
                state = DebugState::Configuring;
                for (const auto& request : requests)
                {
                    if (request.command == "setBreakpoints" || request.command == "configurationDone")
                    {
                        return;
                    }
                }
                Send("configurationDone", Json::object(), {}, configurationGeneration);
            }
        }
        void ReadBreakpoint(Breakpoint& breakpoint, const Json& value)
        {
            breakpoint.verified = value.value("verified", false);
            breakpoint.actualLine = value.value("line", breakpoint.requestedLine.Get());
            breakpoint.message = value.value("message", "");
        }
        void Reply(const Json& message)
        {
            if (message.value("type", "") == "event")
            {
                const auto event = message.value("event", "");
                const Json body = message.value("body", Json::object());
                if (event == "initialized")
                {
                    initialized = true;
                }
                else if (event == "breakpoint")
                {
                    const Json breakpoint = body.value("breakpoint", Json::object());
                    const Int32 id = breakpoint.value("id", 0);
                    for (auto& document : documents)
                    {
                        for (std::size_t index = 0; index < document.ids.Size(); ++index)
                        {
                            if (id != 0 && document.ids[index] == id)
                            {
                                ReadBreakpoint(document.breakpoints[index], breakpoint);
                                ++revision;
                            }
                        }
                    }
                }
                else if (event == "stopped" && state != DebugState::Stopping && state != DebugState::Failed)
                {
                    state = DebugState::Stopped;
                    threadId = body.value("threadId", 0u);
                    ++revision;
                }
                else if (event == "continued" && state == DebugState::Stopped)
                {
                    state = DebugState::Ready;
                    ++revision;
                }
                else if (event == "exited" || event == "terminated")
                {
                    Invalidate();
                    CloseChannel();
                }
                return;
            }
            if (message.value("type", "") != "response")
            {
                return;
            }
            const Int32 sequence = message.value("request_seq", 0);
            for (std::size_t index = 0; index < requests.Size(); ++index)
            {
                if (requests[index].seq != sequence)
                {
                    continue;
                }
                const Request request = requests[index];
                requests.RemoveAt(index);
                if (request.command == "disconnect")
                {
                    CloseChannel();
                    return;
                }
                if (request.command == "setBreakpoints")
                {
                    const auto* document = Find(request.path);
                    if (document == nullptr || document->generation != request.generation)
                    {
                        return;
                    }
                }
                if (!message.value("success", false))
                {
                    Fail(message.value("message", "The debug adapter rejected a request"));
                    return;
                }
                if (request.command == "initialize")
                {
                    Send("attach", {{"processId", processId.Get()}});
                }
                else if (request.command == "attach")
                {
                    attached = true;
                }
                else if (request.command == "setBreakpoints")
                {
                    auto* document = Find(request.path);
                    const Json values = message.value("body", Json::object()).value("breakpoints", Json::array());
                    if (!values.is_array() || values.size() != document->breakpoints.Size())
                    {
                        Fail("The debugger sent an invalid breakpoint response");
                        return;
                    }
                    document->ids.Clear();
                    for (std::size_t at = 0; at < values.size(); ++at)
                    {
                        document->ids.Add(values[at].value("id", 0));
                        ReadBreakpoint(document->breakpoints[at], values[at]);
                    }
                    ++revision;
                }
                else if (request.command == "configurationDone" && request.generation == configurationGeneration)
                {
                    configure = false;
                    state = DebugState::Ready;
                    ++revision;
                }
                else if (request.command == "continue" && state == DebugState::Stopped)
                {
                    state = DebugState::Ready;
                    ++revision;
                }
                return;
            }
        }
    };
    DebugClient::DebugClient(IPlatform& platform) : m_state(MakeOwnerPtr<State>(platform)) {}
    DebugClient::~DebugClient() = default;
    Bool DebugClient::Start(const String& executable, UInt32 processId)
    {
        State& state = *m_state;
        if (state.channel.Get() != nullptr)
        {
            state.error = "The previous debugger session is still closing";
            return false;
        }
        state.error.clear();
        state.incoming.clear();
        state.requests.Clear();
        state.nextSeq = 1;
        state.processId = processId;
        state.threadId = 0;
        state.startupAge = state.cleanupAge = 0.0f;
        state.configure = state.attached = state.initialized = false;
        state.state = DebugState::Starting;
        state.Invalidate();
        if (processId == 0 || executable.empty() || executable.find('"') != String::npos)
        {
            state.Fail("A valid debug adapter path and editor process are required");
            return false;
        }
        state.channel = state.platform->StartProcessChannel(("\"" + executable + "\"").c_str(), nullptr);
        if (state.channel.Get() == nullptr)
        {
            state.Fail("JBroDebugAdapter.exe could not be started");
            return false;
        }
        return state.Send("initialize", {{"clientID", "JBroEngine"}, {"adapterID", "jbro"}, {"pathFormat", "path"},
            {"linesStartAt1", true}, {"columnsStartAt1", true}});
    }
    void DebugClient::Stop()
    {
        if (m_state->state != DebugState::Idle && m_state->state != DebugState::Stopping && m_state->state != DebugState::Failed)
        {
            m_state->Disconnect(false);
        }
    }
    void DebugClient::Poll(Float elapsed)
    {
        State& state = *m_state;
        if (state.channel.Get() == nullptr)
        {
            return;
        }
        String bytes;
        if (state.channel->Read(bytes, true) && !bytes.empty() && state.error.empty())
        {
            state.error = bytes.substr(0, 4096);
        }
        if (state.channel->Read(bytes))
        {
            state.incoming += bytes;
        }
        if (state.incoming.size() > MaxBody + MaxHeader)
        {
            state.Fail("The debugger output exceeded its frame limit");
            state.incoming.clear();
        }
        std::size_t messages = 0;
        while (!state.incoming.empty() && messages++ < 128 && state.channel.Get() != nullptr)
        {
            const auto end = state.incoming.find("\r\n\r\n");
            if (end == String::npos)
            {
                if (state.incoming.size() > MaxHeader)
                {
                    state.Fail("The debugger sent an invalid DAP header");
                    state.incoming.clear();
                }
                break;
            }
            const auto lengthAt = state.incoming.find("Content-Length:");
            const auto duplicateLength = state.incoming.find("\r\nContent-Length:", lengthAt == String::npos ? 0 : lengthAt + 15);
            if (end > MaxHeader || lengthAt == String::npos || lengthAt > end
                || (duplicateLength != String::npos && duplicateLength < end)
                || (lengthAt != 0 && (lengthAt < 2 || state.incoming.View().substr(lengthAt - 2, 2) != "\r\n")))
            {
                state.Fail("The debugger sent an invalid DAP header");
                state.incoming.clear();
                break;
            }
            std::size_t numberAt = lengthAt + 15;
            while (numberAt < end && state.incoming[numberAt] == ' ')
            {
                ++numberAt;
            }
            const auto numberEnd = state.incoming.find("\r\n", numberAt);
            std::size_t length = 0;
            const auto parsed = std::from_chars(state.incoming.data() + numberAt, state.incoming.data() + numberEnd, length);
            if (parsed.ec != std::errc() || parsed.ptr != state.incoming.data() + numberEnd || length > MaxBody)
            {
                state.Fail("The debugger sent an invalid DAP frame length");
                state.incoming.clear();
                break;
            }
            if (state.incoming.size() < end + 4 + length)
            {
                break;
            }
            const Json message = Json::parse(state.incoming.substr(end + 4, length), nullptr, false);
            state.incoming.erase(0, end + 4 + length);
            if (!message.is_object() || message.is_discarded())
            {
                state.Fail("The debugger sent an invalid DAP message");
                continue;
            }
            try
            {
                state.Reply(message);
            }
            catch (const Json::exception&)
            {
                state.Fail("The debugger sent a malformed DAP response");
            }
        }
        if (state.channel.Get() == nullptr)
        {
            return;
        }
        if (state.state == DebugState::Stopping || state.state == DebugState::Failed)
        {
            state.cleanupAge += elapsed;
            if (state.cleanupAge >= 2.0f || !state.channel->IsRunning())
            {
                state.CloseChannel();
            }
            return;
        }
        if (!state.channel->IsRunning())
        {
            state.Fail("The debug adapter stopped unexpectedly");
            return;
        }
        state.Flush();
        if (state.state == DebugState::Starting || state.state == DebugState::Configuring)
        {
            state.startupAge += elapsed;
            if (state.startupAge >= 15.0f)
            {
                state.Fail("The debug adapter did not finish installing breakpoints in time");
            }
        }
    }
    void DebugClient::SetBreakpoints(const String& path, const Array<UInt32>& lines)
    {
        State& state = *m_state;
        const String normalized = EditorPaths::Normalize(path);
        auto* document = state.Find(normalized);
        if (document == nullptr)
        {
            State::Document value;
            value.path = normalized;
            state.documents.Add(std::move(value));
            document = &state.documents.Last();
        }
        document->breakpoints.Clear();
        document->ids.Clear();
        for (UInt32 line : lines)
        {
            if (line == 0)
            {
                continue;
            }
            Bool duplicate = false;
            for (const auto& existing : document->breakpoints)
            {
                duplicate = duplicate || existing.requestedLine == line;
            }
            if (!duplicate)
            {
                Breakpoint breakpoint;
                breakpoint.requestedLine = line;
                document->breakpoints.Add(std::move(breakpoint));
            }
        }
        ++document->generation;
        ++state.configurationGeneration;
        ++state.revision;
        document->dirty = true;
        state.Flush();
    }
    void DebugClient::Configure()
    {
        if (m_state->state == DebugState::Starting || m_state->state == DebugState::Configuring)
        {
            m_state->configure = true;
            m_state->Flush();
        }
    }
    void DebugClient::Continue()
    {
        if (m_state->state == DebugState::Stopped)
        {
            m_state->Send("continue", {{"threadId", m_state->threadId.Get()}});
        }
    }
    DebugState DebugClient::GetState() const { return m_state->state; }
    const Array<Breakpoint>* DebugClient::GetBreakpoints(const String& path) const
    {
        const auto* document = m_state->Find(path);
        return document != nullptr ? &document->breakpoints : nullptr;
    }
    const String& DebugClient::GetError() const { return m_state->error; }
    UInt64 DebugClient::GetRevision() const { return m_state->revision; }
}
