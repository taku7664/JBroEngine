#include "Session.h"

#include <charconv>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <JBro/Types/Table.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace JBro::ScriptLanguageServer
{
    namespace
    {
        using namespace Detail;

        struct InputMessage
        {
            Json Message;
            String Problem;
        };

        class InputQueue final
        {
        public:
            void Push(InputMessage message)
            {
                {
                    const std::lock_guard<std::mutex> lock(m_mutex);
                    m_messages.Add(std::move(message));
                }
                m_changed.notify_one();
            }

            Bool Pop(InputMessage& message)
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_changed.wait(lock, [&] { return m_finished || false == m_messages.IsEmpty(); });
                if (m_messages.IsEmpty())
                {
                    return false;
                }
                message = std::move(m_messages.First());
                m_messages.RemoveAt(0);
                return true;
            }

            void Finish()
            {
                {
                    const std::lock_guard<std::mutex> lock(m_mutex);
                    m_finished = true;
                }
                m_changed.notify_one();
            }

            void Cancel(const Json& id)
            {
                const std::lock_guard<std::mutex> lock(m_mutex);
                m_cancelled.InsertOrAssign(String(id.dump()), true);
            }

            Bool Cancelled(const Json& id, Bool remove = false)
            {
                const std::lock_guard<std::mutex> lock(m_mutex);
                const String key(id.dump());
                const Bool cancelled = m_cancelled.Contains(key);
                if (remove)
                {
                    m_cancelled.Remove(key);
                }
                return cancelled;
            }

            Bool PendingChanges()
            {
                const std::lock_guard<std::mutex> lock(m_mutex);
                for (const InputMessage& pending : m_messages)
                {
                    const String method = JsonString(pending.Message, "method");
                    if (method == "textDocument/didChange" || method == "textDocument/didOpen" || method == "textDocument/didClose" ||
                        method == "workspace/didChangeWatchedFiles" || method == "workspace/didDeleteFiles" ||
                        method == "workspace/didCreateFiles" || method == "workspace/didRenameFiles")
                    {
                        return true;
                    }
                    // A request preceding the next edit must observe the preceding version.
                    if (pending.Message.contains("id"))
                    {
                        break;
                    }
                }
                return false;
            }

        private:
            std::mutex m_mutex;
            std::condition_variable m_changed;
            Array<InputMessage> m_messages;
            Table<String, Bool> m_cancelled;
            Bool m_finished = false;
        };

        enum class FrameState { Complete, End, Invalid, Fatal };

        FrameState ReadFrame(std::istream& input, String& body, String& error)
        {
            constexpr std::size_t MaxHeader = 8192;
            constexpr std::size_t MaxBody = 16 * 1024 * 1024;
            String header;
            char character = 0;
            while (input.get(character))
            {
                header += character;
                if (header.EndsWith("\r\n\r\n"))
                {
                    break;
                }
                if (header.size() > MaxHeader)
                {
                    error = "LSP header exceeds 8192 bytes.";
                    return FrameState::Fatal;
                }
            }
            if (header.IsEmpty() && input.eof())
            {
                return FrameState::End;
            }
            if (false == header.EndsWith("\r\n\r\n"))
            {
                error = "Truncated LSP header.";
                return FrameState::Fatal;
            }
            Bool foundLength = false;
            Bool invalidHeader = false;
            std::size_t length = 0;
            for (String line : header.Split('\n', true))
            {
                line.Trim();
                if (line.IsEmpty())
                {
                    continue;
                }
                const std::size_t colon = line.find(':');
                if (colon == String::npos)
                {
                    invalidHeader = true;
                    continue;
                }
                const String key = line.Substr(0, colon).Trim().ToLowerCopy();
                const String value = line.Substr(colon + 1).Trim();
                if (key == "content-length")
                {
                    std::size_t parsed = 0;
                    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
                    if (foundLength || value.IsEmpty() || result.ec != std::errc() || result.ptr != value.data() + value.size() || parsed > MaxBody)
                    {
                        error = "Invalid or duplicate Content-Length; cannot safely continue framing.";
                        return FrameState::Fatal;
                    }
                    foundLength = true;
                    length = parsed;
                }
            }
            if (false == foundLength)
            {
                error = "Missing Content-Length; cannot safely continue framing.";
                return FrameState::Fatal;
            }
            body.resize(length);
            input.read(body.data(), static_cast<std::streamsize>(length));
            if (static_cast<std::size_t>(input.gcount()) != length)
            {
                error = "Truncated LSP message body.";
                return FrameState::Fatal;
            }
            if (invalidHeader)
            {
                error = "Malformed LSP header.";
                return FrameState::Invalid;
            }
            return FrameState::Complete;
        }

        void ReadInput(std::istream& input, InputQueue& queue)
        {
            try
            {
                while (true)
                {
                    String body;
                    String error;
                    const FrameState frame = ReadFrame(input, body, error);
                    if (frame == FrameState::End)
                    {
                        break;
                    }
                    if (frame != FrameState::Complete)
                    {
                        queue.Push({nullptr, std::move(error)});
                        if (frame == FrameState::Fatal)
                        {
                            break;
                        }
                        continue;
                    }
                    Json message = Json::parse(body.begin(), body.end(), nullptr, false);
                    if (message.is_discarded())
                    {
                        queue.Push({nullptr, "Invalid JSON message."});
                        continue;
                    }
                    if (message.is_object() && JsonString(message, "jsonrpc") == "2.0" && false == message.contains("id") &&
                        JsonString(message, "method") == "$/cancelRequest" && message.contains("params") &&
                        message["params"].is_object() && message["params"].contains("id") &&
                        (message["params"]["id"].is_string() || message["params"]["id"].is_number_integer()))
                    {
                        queue.Cancel(message["params"]["id"]);
                        continue;
                    }
                    const Bool exit = message.is_object() && JsonString(message, "jsonrpc") == "2.0" && false == message.contains("id") &&
                        JsonString(message, "method") == "exit" && (false == message.contains("params") || message["params"].is_object());
                    queue.Push({std::move(message), {}});
                    if (exit)
                    {
                        break;
                    }
                }
            }
            catch (const std::exception& exception)
            {
                queue.Push({nullptr, String("Protocol input failure: ").Append(exception.what())});
            }
            queue.Finish();
        }

        void Send(std::ostream& output, Json message)
        {
            const String body(message.dump(-1, ' ', false, Json::error_handler_t::replace));
            output << "Content-Length: " << body.size() << "\r\n\r\n";
            output.write(body.data(), static_cast<std::streamsize>(body.size()));
            output.flush();
        }

        void Error(std::ostream& output, const Json& id, Int32 code, std::string_view message)
        {
            Send(output, Json{{"jsonrpc", "2.0"}, {"id", id}, {"error", Json{{"code", code}, {"message", String(message).Std()}}}});
        }

        Json Capabilities(Bool versionedEdits)
        {
            return Json{{"positionEncoding", "utf-16"},
                {"textDocumentSync", Json{{"openClose", true}, {"change", 2}, {"save", Json{{"includeText", false}}}}},
                {"completionProvider", Json{{"triggerCharacters", Json::array({".", "?"})}}},
                {"hoverProvider", true}, {"definitionProvider", true}, {"typeDefinitionProvider", true},
                {"documentSymbolProvider", true}, {"workspaceSymbolProvider", true}, {"referencesProvider", true},
                {"renameProvider", versionedEdits ? Json{{"prepareProvider", true}} : Json(false)},
                {"signatureHelpProvider", Json{{"triggerCharacters", Json::array({"(", ","})}, {"retriggerCharacters", Json::array({")"})}}},
                {"semanticTokensProvider", Json{{"legend", Json{
                    {"tokenTypes", Json::array({"type", "class", "struct", "interface", "enum", "enumMember", "function", "method", "property", "parameter", "variable", "namespace", "keyword", "string", "number", "comment", "operator"})},
                    {"tokenModifiers", Json::array({"declaration", "readonly", "static", "defaultLibrary"})}}}, {"full", true}, {"range", false}}},
                {"foldingRangeProvider", true}, {"documentFormattingProvider", true},
                {"workspace", Json{{"workspaceFolders", Json{{"supported", false}, {"changeNotifications", false}}}}}};
        }

        Bool FeatureMethod(std::string_view method)
        {
            for (const char* available : {"textDocument/completion", "textDocument/hover", "textDocument/definition", "textDocument/typeDefinition",
                "textDocument/documentSymbol", "workspace/symbol", "textDocument/signatureHelp", "textDocument/references", "textDocument/rename",
                "textDocument/prepareRename", "textDocument/semanticTokens/full", "textDocument/foldingRange", "textDocument/formatting"})
            {
                if (method == available)
                {
                    return true;
                }
            }
            return false;
        }

        Bool DocumentNotification(std::string_view method)
        {
            for (const char* available : {"textDocument/didOpen", "textDocument/didChange", "textDocument/didSave", "textDocument/didClose",
                "workspace/didChangeWatchedFiles", "workspace/didCreateFiles", "workspace/didDeleteFiles", "workspace/didRenameFiles"})
            {
                if (method == available)
                {
                    return true;
                }
            }
            return false;
        }
    }

    Int32 Run(std::istream& input, std::ostream& output, std::ostream& log, ServerOptions options)
    {
        InputQueue queue;
        Session session(std::move(options));
        std::jthread reader([&] { ReadInput(input, queue); });
        Bool initialized = false;
        Bool ready = false;
        Bool shutdown = false;
        Bool dirtyDiagnostics = false;
        Bool dynamicWatching = false;
        Bool relativeWatchPatterns = false;
        const Json watchRegistrationId = "jbro-lsp/register-watchers";
        Int32 exitCode = 1;
        InputMessage incoming;
        while (queue.Pop(incoming))
        {
            if (incoming.Problem.IsNotEmpty())
            {
                Error(output, nullptr, -32700, incoming.Problem.View());
                log << "jbroc LSP: " << incoming.Problem << '\n';
                continue;
            }
            const Json& message = incoming.Message;
            if (message.is_object() && JsonString(message, "jsonrpc") == "2.0" && false == message.contains("method") &&
                message.contains("id") && (message.contains("result") || message.contains("error")))
            {
                if (message["id"] == watchRegistrationId && message.contains("error"))
                {
                    log << "jbroc LSP: client rejected file watcher registration: " << message["error"].dump() << '\n';
                }
                continue;
            }
            const Bool hasId = message.is_object() && message.contains("id");
            const Json id = hasId ? message["id"] : Json(nullptr);
            const String method = message.is_object() ? JsonString(message, "method") : String();
            if (false == message.is_object() || JsonString(message, "jsonrpc") != "2.0" || method.IsEmpty() ||
                (hasId && false == id.is_string() && false == id.is_number_integer()) ||
                (message.contains("params") && false == message["params"].is_object()))
            {
                Error(output, hasId && (id.is_string() || id.is_number_integer()) ? id : Json(nullptr), -32600, "Invalid JSON-RPC request.");
                continue;
            }
            if (method == "exit")
            {
                if (hasId)
                {
                    Error(output, id, -32600, "exit must be a notification.");
                    continue;
                }
                exitCode = shutdown ? 0 : 1;
                break;
            }
            if (shutdown)
            {
                if (hasId)
                {
                    Error(output, id, -32600, "The server has shut down.");
                }
                continue;
            }
            const Json params = message.value("params", Json::object());
            try
            {
                if (method == "initialize")
                {
                    if (false == hasId)
                    {
                        log << "jbroc LSP: initialize must be a request.\n";
                        continue;
                    }
                    if (initialized)
                    {
                        Error(output, id, -32600, "The server is already initialized.");
                        continue;
                    }
                    String error;
                    if (false == session.Initialize(params, error))
                    {
                        Error(output, id, -32602, error.View());
                        continue;
                    }
                    const Json clientCapabilities = params.value("capabilities", Json::object());
                    const Json workspaceCapabilities = clientCapabilities.value("workspace", Json::object());
                    const Json watchCapabilities = workspaceCapabilities.value("didChangeWatchedFiles", Json::object());
                    dynamicWatching = watchCapabilities.value("dynamicRegistration", false);
                    relativeWatchPatterns = watchCapabilities.value("relativePatternSupport", false);
                    initialized = true;
                    Send(output, Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", Json{
                        {"capabilities", Capabilities(session.SupportsVersionedEdits())}, {"serverInfo", Json{{"name", "JBroScript Language Server"}}}}}});
                }
                else if (false == initialized)
                {
                    if (hasId)
                    {
                        Error(output, id, -32002, "The server is not initialized.");
                    }
                }
                else if (method == "initialized")
                {
                    if (hasId)
                    {
                        Error(output, id, -32600, "initialized must be a notification.");
                    }
                    else
                    {
                        if (false == ready && dynamicWatching)
                        {
                            Send(output, Json{{"jsonrpc", "2.0"}, {"id", watchRegistrationId}, {"method", "client/registerCapability"},
                                {"params", Json{{"registrations", Json::array({Json{{"id", "jbro-lsp/script-files"},
                                    {"method", "workspace/didChangeWatchedFiles"}, {"registerOptions", session.WatchOptions(relativeWatchPatterns)}}})}}}});
                        }
                        ready = true;
                    }
                }
                else if (method == "shutdown")
                {
                    if (hasId)
                    {
                        shutdown = true;
                        Send(output, Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", nullptr}});
                    }
                }
                else if (DocumentNotification(method.View()))
                {
                    if (hasId)
                    {
                        Error(output, id, -32600, "Document synchronization methods must be notifications.");
                        continue;
                    }
                    String error;
                    if (false == session.ChangeDocument(method.View(), params, error))
                    {
                        log << "jbroc LSP: " << method << ": " << error << '\n';
                        Send(output, Json{{"jsonrpc", "2.0"}, {"method", "window/logMessage"}, {"params", Json{{"type", 1}, {"message", error.Std()}}}});
                    }
                    else
                    {
                        dirtyDiagnostics = true;
                    }
                }
                else if (hasId)
                {
                    if (queue.Cancelled(id))
                    {
                        Error(output, id, -32800, "Request cancelled.");
                    }
                    else if (FeatureMethod(method.View()))
                    {
                        Int32 code = 0;
                        String error;
                        Json result = session.Request(method.View(), params, code, error);
                        if (queue.Cancelled(id))
                        {
                            Error(output, id, -32800, "Request cancelled.");
                        }
                        else if (code != 0)
                        {
                            Error(output, id, code, error.View());
                        }
                        else
                        {
                            Send(output, Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}});
                        }
                    }
                    else
                    {
                        Error(output, id, -32601, "Method not found.");
                    }
                    queue.Cancelled(id, true);
                }
                if (ready && false == shutdown && dirtyDiagnostics && false == queue.PendingChanges())
                {
                    const Array<Json> diagnostics = session.Diagnostics();
                    if (false == queue.PendingChanges())
                    {
                        for (const Json& diagnostic : diagnostics)
                        {
                            Send(output, Json{{"jsonrpc", "2.0"}, {"method", "textDocument/publishDiagnostics"}, {"params", diagnostic}});
                        }
                        session.ClearedUris.Clear();
                        dirtyDiagnostics = false;
                    }
                }
            }
            catch (const Json::exception& exception)
            {
                if (hasId)
                {
                    Error(output, id, -32602, "Invalid method parameters.");
                }
                log << "jbroc LSP: invalid parameters for " << method << ": " << exception.what() << '\n';
            }
            catch (const std::exception& exception)
            {
                if (hasId)
                {
                    Error(output, id, -32603, "Language server internal error.");
                }
                log << "jbroc LSP: " << exception.what() << '\n';
            }
        }
        if (shutdown)
        {
            exitCode = 0;
        }
        return exitCode;
    }

    Int32 RunStdio(ServerOptions options)
    {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        // The reader thread must not flush the writer's stream through cin's default tie.
        std::cin.tie(nullptr);
        return Run(std::cin, std::cout, std::cerr, std::move(options));
    }
}
