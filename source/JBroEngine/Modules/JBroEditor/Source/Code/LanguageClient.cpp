#include <JBro/Editor/Code/LanguageClient.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Editor/EditorPaths.h>
#include <JBro/Core/Log.h>
#include <nlohmann/json.hpp>
#include <charconv>

namespace JBro::Code
{
    using Json = nlohmann::json;
    namespace
    {
        Position Point(const Json& value)
        {
            return {value.value("line", 0u), value.value("character", 0u)};
        }
        Range ReadRange(const Json& value)
        {
            return {Point(value.value("start", Json::object())), Point(value.value("end", Json::object()))};
        }
    }
    struct LanguageClient::State
    {
        struct Document
        {
            String path, uri, text;
            Analysis analysis;
            Int32 version = 1;
            Bool opened = false, changed = false;
            Float age = 0.0f;
        };
        struct Request
        {
            int id = 0;
            String uri;
            Int32 version = 0;
        };
        IPlatform* platform;
        OwnerPtr<IProcessChannel> channel;
        Array<Document> documents;
        Array<Request> requests;
        String incoming, error;
        ClientState state = ClientState::Stopped;
        int nextId = 2;
        UInt64 revision = 0;
        Float startupAge = 0.0f;
        explicit State(IPlatform& value) : platform(&value) {}
        Document* Find(const String& path)
        {
            const String normalized = LanguageClient::NormalizePath(path);
            for (auto& document : documents)
            {
                if (document.path == normalized)
                {
                    return &document;
                }
            }
            return nullptr;
        }
        void Fail(const String& message)
        {
            error = message;
            state = ClientState::Failed;
            channel = nullptr;
            for (auto& document : documents)
            {
                document.analysis.diagnostics.Clear();
                document.analysis.tokens.Clear();
                document.analysis.revision = ++revision;
                document.opened = false;
            }
            Log::Write(LogLevel::Warning, "script", "JBroScript language server: %s", error.c_str());
        }
        Bool Send(Json message)
        {
            message["jsonrpc"] = "2.0";
            const String body = message.dump();
            const String frame = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
            if (channel.Get() == nullptr || !channel->Write(frame.View()))
            {
                Fail("The language server input channel is unavailable");
                return false;
            }
            return true;
        }
        void Notify(const char* method, Json params)
        {
            Send({{"method", method}, {"params", std::move(params)}});
        }
        void Semantic(Document& document)
        {
            const int id = nextId++;
            requests.Add({id, document.uri, document.version});
            Send({{"id", id}, {"method", "textDocument/semanticTokens/full"},
                {"params", {{"textDocument", {{"uri", document.uri.Std()}}}}}});
        }
        void OpenPending()
        {
            for (auto& document : documents)
            {
                if (state != ClientState::Ready)
                {
                    return;
                }
                if (!document.opened)
                {
                    Notify("textDocument/didOpen", {{"textDocument", {{"uri", document.uri.Std()},
                        {"languageId", "jbroscript"}, {"version", document.version.Get()}, {"text", document.text.Std()}}}});
                    if (state != ClientState::Ready)
                    {
                        return;
                    }
                    document.opened = true;
                    document.changed = false;
                    Semantic(document);
                }
            }
        }
        void Reply(const Json& message)
        {
            if (message.value("method", "") == "textDocument/publishDiagnostics")
            {
                const auto& params = message["params"];
                const String uri = params.value("uri", "");
                for (auto& document : documents)
                {
                    if (document.uri != uri || params.value("version", -1) != document.version.Get())
                    {
                        continue;
                    }
                    document.analysis.diagnostics.Clear();
                    for (const auto& entry : params.value("diagnostics", Json::array()))
                    {
                        Diagnostic diagnostic;
                        diagnostic.range = ReadRange(entry.value("range", Json::object()));
                        diagnostic.severity = entry.value("severity", 1u);
                        diagnostic.message = entry.value("message", "");
                        if (entry.contains("code"))
                        {
                            diagnostic.code = entry["code"].is_string() ? entry["code"].get<std::string>() : entry["code"].dump();
                        }
                        document.analysis.diagnostics.Add(std::move(diagnostic));
                    }
                    document.analysis.version = document.version;
                    document.analysis.revision = ++revision;
                }
                return;
            }
            if (!message.contains("id") || !message["id"].is_number_integer())
            {
                return;
            }
            const int id = message["id"].get<int>();
            if (id == 1)
            {
                if (message.contains("error"))
                {
                    Fail(message["error"].value("message", "Language server initialization failed"));
                    return;
                }
                state = ClientState::Ready;
                Notify("initialized", Json::object());
                OpenPending();
                return;
            }
            for (std::size_t at = 0; at < requests.Size(); ++at)
            {
                if (requests[at].id != id)
                {
                    continue;
                }
                const Request request = requests[at];
                requests.RemoveAt(at);
                for (auto& document : documents)
                {
                    if (document.uri != request.uri || document.version != request.version || !message.contains("result"))
                    {
                        continue;
                    }
                    document.analysis.tokens.Clear();
                    const Json data = message["result"].is_null() ? Json::array() : message["result"].value("data", Json::array());
                    if (!data.is_array() || data.size() % 5 != 0)
                    {
                        Fail("The language server sent invalid semantic tokens");
                        return;
                    }
                    UInt32 line = 0, character = 0;
                    for (std::size_t i = 0; i + 4 < data.size(); i += 5)
                    {
                        const auto delta = data[i].get<std::uint32_t>();
                        line += delta;
                        character = delta != 0 ? UInt32(0) : character;
                        character += data[i + 1].get<std::uint32_t>();
                        SemanticToken token;
                        token.range = {{line, character}, {line, character + data[i + 2].get<std::uint32_t>()}};
                        token.kind = data[i + 3].get<std::uint32_t>();
                        token.modifiers = data[i + 4].get<std::uint32_t>();
                        document.analysis.tokens.Add(token);
                    }
                    document.analysis.version = document.version;
                    document.analysis.revision = ++revision;
                }
                return;
            }
        }
    };
    LanguageClient::LanguageClient(IPlatform& platform) : m_state(MakeOwnerPtr<State>(platform)) {}
    LanguageClient::~LanguageClient() = default;
    Bool LanguageClient::Start(const String& executable, const String& project, const String& locale)
    {
        Stop();
        m_state->channel = m_state->platform->StartProcessChannel(("\"" + executable + "\" --lsp").c_str(), nullptr);
        if (m_state->channel.Get() == nullptr)
        {
            m_state->Fail("JBroc.exe could not be started");
            return false;
        }
        m_state->state = ClientState::Starting;
        m_state->startupAge = 0.0f;
        return m_state->Send({{"id", 1}, {"method", "initialize"}, {"params", {
            {"locale", locale.Std()}, {"initializationOptions", {{"projectFile", project.Std()}}},
            {"capabilities", {{"general", {{"positionEncodings", Json::array({"utf-16"})}}},
                {"textDocument", {{"publishDiagnostics", {{"versionSupport", true}}}}}}}}}});
    }
    void LanguageClient::Stop()
    {
        m_state->channel = nullptr;
        m_state->incoming.clear();
        m_state->requests.Clear();
        m_state->error.clear();
        m_state->state = ClientState::Stopped;
        m_state->nextId = 2;
        for (auto& document : m_state->documents)
        {
            document.opened = false;
            document.analysis.diagnostics.Clear();
            document.analysis.tokens.Clear();
            document.analysis.revision = ++m_state->revision;
        }
    }
    void LanguageClient::Poll(Float elapsed)
    {
        State& state = *m_state;
        if (state.channel.Get() == nullptr)
        {
            return;
        }
        String bytes;
        if (state.channel->Read(bytes, true))
        {
            Log::Write(LogLevel::Info, "script", "%s", bytes.c_str());
        }
        if (state.channel->Read(bytes))
        {
            state.incoming += bytes;
        }
        while (!state.incoming.empty())
        {
            const auto end = state.incoming.find("\r\n\r\n");
            if (end == String::npos)
            {
                if (state.incoming.size() > 8192)
                {
                    state.Fail("The language server sent an invalid frame header");
                }
                break;
            }
            const auto start = state.incoming.find("Content-Length: ");
            std::size_t length = 0;
            if (end > 8192 || start == String::npos || start > end
                || (start != 0 && (start < 2 || state.incoming.View().substr(start - 2, 2) != "\r\n")))
            {
                state.Fail("The language server sent an invalid frame length");
                break;
            }
            const auto lengthEnd = state.incoming.find("\r\n", start);
            const auto parsed = std::from_chars(state.incoming.data() + start + 16, state.incoming.data() + lengthEnd, length);
            if (parsed.ec != std::errc() || parsed.ptr != state.incoming.data() + lengthEnd || length > 8 * 1024 * 1024)
            {
                state.Fail("The language server sent an invalid frame length");
                break;
            }
            if (state.incoming.size() < end + 4 + length)
            {
                break;
            }
            const Json message = Json::parse(state.incoming.substr(end + 4, length), nullptr, false);
            state.incoming.erase(0, end + 4 + length);
            if (message.is_discarded() || !message.is_object())
            {
                state.Fail("The language server sent an invalid JSON-RPC message");
                break;
            }
            try
            {
                state.Reply(message);
            }
            catch (const Json::exception&)
            {
                state.Fail("The language server sent a malformed response");
            }
            if (state.state == ClientState::Failed)
            {
                break;
            }
        }
        if (state.channel.Get() == nullptr)
        {
            return;
        }
        if (!state.channel->IsRunning())
        {
            state.Fail("The language server stopped. Reconnect to resume analysis");
            return;
        }
        if (state.state == ClientState::Starting)
        {
            state.startupAge += elapsed;
            if (state.startupAge > 15.0f)
            {
                state.Fail("The language server did not initialize in time");
            }
            return;
        }
        state.OpenPending();
        Bool refresh = false;
        for (auto& document : state.documents)
        {
            document.age += elapsed;
            if (document.changed && document.age >= 0.15f)
            {
                state.Notify("textDocument/didChange", {{"textDocument", {{"uri", document.uri.Std()}, {"version", document.version.Get()}}},
                    {"contentChanges", Json::array({{{"text", document.text.Std()}}})}});
                document.changed = false;
                refresh = true;
            }
        }
        if (refresh)
        {
            for (auto& document : state.documents)
            {
                state.Semantic(document);
            }
        }
    }
    void LanguageClient::Open(const String& path, const String& text)
    {
        if (m_state->Find(path) != nullptr)
        {
            Change(path, text);
            return;
        }
        State::Document document;
        document.path = NormalizePath(path);
        document.uri = FileUri(document.path);
        document.text = text;
        m_state->documents.Add(std::move(document));
    }
    void LanguageClient::Change(const String& path, const String& text)
    {
        if (auto* document = m_state->Find(path); document != nullptr && document->text != text)
        {
            document->text = text;
            ++document->version;
            document->changed = true;
            document->age = 0.0f;
            document->analysis.diagnostics.Clear();
            document->analysis.tokens.Clear();
            document->analysis.revision = ++m_state->revision;
        }
    }
    void LanguageClient::Saved(const String& path)
    {
        const auto* document = m_state->Find(path);
        if (document != nullptr && document->opened && m_state->state == ClientState::Ready)
        {
            m_state->Notify("textDocument/didSave", {{"textDocument", {{"uri", document->uri.Std()}}}});
        }
    }
    void LanguageClient::Close(const String& path)
    {
        const auto* document = m_state->Find(path);
        for (std::size_t i = 0; document != nullptr && i < m_state->documents.Size(); ++i)
        {
            if (&m_state->documents[i] == document)
            {
                if (document->opened && m_state->state == ClientState::Ready)
                {
                    m_state->Notify("textDocument/didClose", {{"textDocument", {{"uri", document->uri.Std()}}}});
                }
                m_state->documents.RemoveAt(i);
                return;
            }
        }
    }
    const Analysis* LanguageClient::GetAnalysis(const String& path) const
    {
        const auto* document = m_state->Find(path);
        return document != nullptr ? &document->analysis : nullptr;
    }
    ClientState LanguageClient::GetState() const { return m_state->state; }
    const String& LanguageClient::GetError() const { return m_state->error; }
    String LanguageClient::NormalizePath(const String& path)
    {
        return EditorPaths::Normalize(path);
    }
    String LanguageClient::FileUri(const String& path)
    {
        const String normalized = NormalizePath(path);
        String result = normalized.StartsWith("//") ? "file:" : normalized.StartsWith("/") ? "file://" : "file:///";
        constexpr char hex[] = "0123456789ABCDEF";
        for (unsigned char ch : normalized)
        {
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
                || ch == '/' || ch == ':' || ch == '-' || ch == '_' || ch == '.' || ch == '~')
            {
                result += static_cast<char>(ch);
            }
            else
            {
                result += '%'; result += hex[ch >> 4]; result += hex[ch & 15];
            }
        }
        return result;
    }
}
