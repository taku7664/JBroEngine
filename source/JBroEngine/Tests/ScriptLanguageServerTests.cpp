#include <JBro/ScriptLanguageServer/Server.h>

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace
{
    using namespace JBro;

    void Check(Bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    String Frame(std::string_view body)
    {
        return String("Content-Length: ").Append(std::to_string(body.size())).Append("\r\n\r\n").Append(body);
    }

    class MemoryFiles final : public JBro::ScriptLanguageServer::WorkspaceFiles
    {
    public:
        Bool Read(std::string_view path, String& text) override
        {
            if (path == "C:/project/game.jproject")
            {
                text = "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\n";
                return true;
            }
            return false;
        }
        String Resolve(std::string_view base, std::string_view path) override
        {
            if (path.size() > 2 && path[1] == ':')
            {
                return String(path).ReplaceAll("/./", "/");
            }
            const std::size_t slash = base.find_last_of('/');
            String resolved = String(base.substr(0, slash + 1)).Append(path).ReplaceAll("/./", "/");
            if (resolved.EndsWith("/."))
            {
                resolved.resize(resolved.size() - 2);
            }
            return resolved;
        }
        Bool EnumerateScripts(std::string_view, Array<String>& paths, String&) override
        {
            paths.Clear();
            return true;
        }
    };

    void TestClientLocaleTags()
    {
        using namespace JBro::ScriptLanguageServer;
        struct LocaleTables
        {
            std::filesystem::path Directory = std::filesystem::temp_directory_path()
                / ("JBroLspLocale-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            LocaleTables()
            {
                Check(std::filesystem::create_directory(Directory), "locale probe uses a fresh temporary directory");
                std::ofstream(Directory / "ko-KR.yaml") << "Entries:\n  jbroc.parse.expected_declaration: KOREAN_DECLARATION\n";
                std::ofstream(Directory / "en-US.yaml") << "Entries:\n  jbroc.parse.expected_declaration: ENGLISH_DECLARATION\n";
            }
            ~LocaleTables()
            {
                std::error_code ignored;
                std::filesystem::remove(Directory / "ko-KR.yaml", ignored);
                std::filesystem::remove(Directory / "en-US.yaml", ignored);
                std::filesystem::remove(Directory, ignored);
            }
        } tables;
        const auto directory = tables.Directory.u8string();
        for (const char* locale : {"ko", "KO-kr", "ko-KP", "en", "en-GB", "fr", "../ko-KR", ""})
        {
            MemoryFiles files;
            ServerOptions options;
            options.Files = &files;
            options.LocalizationDirectory = String(std::string_view(
                reinterpret_cast<const char*>(directory.data()), directory.size()));
            String frames = Frame(String(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"locale":")")
                .Append(locale).Append(R"(","initializationOptions":{"projectFile":"C:/project/game.jproject"}}})").View());
            frames.Append(Frame(R"({"jsonrpc":"2.0","method":"initialized","params":{}})"));
            frames.Append(Frame(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///C:/project/Contents/Locale.jscript","version":1,"text":"bad\n"}}})"));
            frames.Append(Frame(R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})"));
            frames.Append(Frame(R"({"jsonrpc":"2.0","method":"exit"})"));
            std::istringstream input(frames.Std());
            std::ostringstream output;
            std::ostringstream log;
            Check(Run(input, output, log, std::move(options)) == 0, "localized protocol session shuts down cleanly");
            const Bool korean = locale[0] == '\0' || String(locale).ToLowerCopy().StartsWith("ko");
            Check(String(output.str()).Contains(korean ? "KOREAN_DECLARATION" : "ENGLISH_DECLARATION"),
                "client language tags select a supported diagnostic table without using the tag as a file path");
        }
    }

    void TestScriptSummaryProtocol()
    {
        using namespace JBro::ScriptLanguageServer;
        using Json = nlohmann::json;
        MemoryFiles files;
        const char* uri = "file:///C:/project/Contents/Summary.jscript";
        const char* source = "class Counter\r\n{\r\n  [name(\"\xF0\x9F\x98\x80\")] Int count = 1 // keep\r\n}\r\n";
        String inputFrames;
        auto send = [&](const Json& message)
        {
            inputFrames.Append(Frame(message.dump()));
        };
        send(Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
            {"params", Json{{"initializationOptions", Json{{"projectFile", "C:/project/game.jproject"}}}}}});
        send(Json{{"jsonrpc", "2.0"}, {"method", "initialized"}, {"params", Json::object()}});
        send(Json{{"jsonrpc", "2.0"}, {"method", "textDocument/didOpen"},
            {"params", Json{{"textDocument", Json{{"uri", uri}, {"version", 7}, {"text", source}}}}}});
        auto request = [&](Int32 id, const char* method, Json params)
        {
            send(Json{{"jsonrpc", "2.0"}, {"id", id.Get()}, {"method", method}, {"params", std::move(params)}});
        };
        const Json current = Json{{"uri", uri}, {"version", 7}};
        request(20, "jbro/scriptSummary", Json{{"textDocument", current}});
        request(21, "jbro/editFieldSettings", Json{{"textDocument", Json{{"uri", uri}, {"version", 6}}},
            {"position", Json{{"line", 2}, {"character", 19}}}, {"changes", Json{{"initializer", "8"}}}});
        request(22, "jbro/editFieldSettings", Json{{"textDocument", current},
            {"position", Json{{"line", 2}, {"character", 19}}}, {"changes", Json{{"initializer", "8"}}}});
        request(23, "jbro/editFieldSettings", Json{{"textDocument", current},
            {"position", Json{{"line", 2}, {"character", 19}}}, {"changes", Json{{"initializer", "1\nInt injected = 2"}}}});
        request(24, "jbro/scriptSummary", Json{{"textDocument", Json{{"uri", uri}}}});
        send(Json{{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
            {"params", Json{{"textDocument", Json{{"uri", uri}, {"version", 8}}},
                {"contentChanges", Json::array({Json{{"text", "class Counter\n{\nInt count = (\n}\n"}}})}}}});
        request(25, "jbro/scriptSummary", Json{{"textDocument", Json{{"uri", uri}, {"version", 8}}}});
        request(26, "jbro/editFieldSettings", Json{{"textDocument", Json{{"uri", uri}, {"version", 8}}},
            {"position", Json{{"line", 2}, {"character", 4}}}, {"changes", Json{{"initializer", "9"}}}});
        request(2, "shutdown", Json::object());
        send(Json{{"jsonrpc", "2.0"}, {"method", "exit"}});
        ServerOptions options;
        options.Files = &files;
        std::istringstream input(inputFrames.Std());
        std::ostringstream output;
        std::ostringstream log;
        Check(Run(input, output, log, std::move(options)) == 0, "script summary protocol session shuts down cleanly");
        std::istringstream frames(output.str());
        Array<Json> responses;
        String header;
        while (std::getline(frames, header.Std()))
        {
            Check(header.StartsWith("Content-Length: "), "summary response framing stays valid");
            const std::size_t length = std::stoull(header.Substr(16).Std());
            std::getline(frames, header.Std());
            String body(length, '\0');
            frames.read(body.data(), static_cast<std::streamsize>(length));
            responses.Add(Json::parse(body));
        }
        auto response = [&](Int32 id) -> Json
        {
            for (const Json& value : responses)
            {
                if (value.contains("id") && value["id"] == id.Get())
                {
                    return value;
                }
            }
            throw std::runtime_error("missing script summary response");
        };
        const Json summary = response(20)["result"];
        Check(summary["valid"] == true && summary["version"] == 7, "summary echoes the exact unsaved buffer version");
        Check(summary["types"][0]["members"][0]["selectionRange"]["start"]["character"] == 19,
            "summary field positions count a preceding emoji as two UTF-16 units");
        Check(response(21)["error"]["code"] == -32801, "stale summary edits return ContentModified");
        const Json edit = response(22)["result"];
        Check(edit["version"] == 7 && edit["edits"].size() == 1 && edit["edits"][0]["newText"] == "8"
            && edit["edits"][0]["range"]["start"]["character"] == 27,
            "settings edits return only the initializer span using UTF-16 positions");
        Check(response(23)["error"]["code"] == -32602 && response(24)["error"]["code"] == -32602,
            "expression injection and requests without document versions are rejected");
        Check(response(25)["result"]["valid"] == false && response(25)["result"]["types"].empty()
            && response(26)["error"]["code"] == -32602, "invalid source has no editable summary or source edits");
    }
}

JBro::Int32 RunScriptLanguageServerTests()
{
    using namespace JBro::ScriptLanguageServer;
    MemoryFiles files;
    ServerOptions options;
    options.Files = &files;
    String messages = Frame(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"initializationOptions":{"projectFile":"C:/project/game.jproject"}}})");
    messages.Append(Frame(R"({"jsonrpc":"2.0","method":"initialized","params":{}})"));
    messages.Append(Frame(R"({"jsonrpc":"2.0","id":"unknown","method":"missing"})"));
    messages.Append(Frame(R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})"));
    messages.Append(Frame(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::istringstream input(messages.Std());
    std::ostringstream output;
    std::ostringstream log;
    Check(Run(input, output, log, std::move(options)) == 0, "LSP shutdown must exit successfully");
    const String result(output.str());
    Check(result.Contains("Content-Length: "), "LSP must frame output");
    Check(result.Contains("positionEncoding"), "LSP must advertise UTF-16 positions");
    Check(result.Contains("semanticTokensProvider"), "LSP must advertise semantic tokens");
    Check(result.Contains("-32601"), "LSP unknown request must return MethodNotFound");
    Check(result.Contains("\"id\":\"unknown\""), "LSP must preserve string request IDs");
    Check(result.Contains("\"result\":null"), "LSP shutdown must return null");
    Check(result.Contains("jbroc: warning") == false, "LSP stdout must contain protocol only");

    std::istringstream truncated("Content-Length: 100\r\n\r\n{}");
    std::ostringstream truncatedOutput;
    std::ostringstream truncatedLog;
    ServerOptions truncatedOptions;
    truncatedOptions.Files = &files;
    Check(Run(truncated, truncatedOutput, truncatedLog, std::move(truncatedOptions)) != 0,
        "truncated protocol input must not report successful shutdown");
    Check(truncatedOutput.str().find("-32700") != std::string::npos,
        "truncated protocol input must report a framed parse error");

    String recovery = Frame("{ broken JSON }");
    recovery.Append(Frame(R"({"jsonrpc":"2.0","id":5,"method":"workspace/symbol","params":{"query":""}})"));
    recovery.Append(messages.View());
    std::istringstream recoveryInput(recovery.Std());
    std::ostringstream recoveryOutput;
    std::ostringstream recoveryLog;
    ServerOptions recoveryOptions;
    recoveryOptions.Files = &files;
    Check(Run(recoveryInput, recoveryOutput, recoveryLog, std::move(recoveryOptions)) == 0,
        "valid frames following invalid JSON must remain usable");
    Check(recoveryOutput.str().find("-32700") != std::string::npos, "invalid JSON must report ParseError");
    Check(recoveryOutput.str().find("-32002") != std::string::npos, "requests before initialize must report ServerNotInitialized");

    const String emojiSource = "class Reader\n{\nfn Read() -> String\n{\nString label = \"\xF0\x9F\x98\x80\" + missing\nreturn label\n}\n}\n";
    const String emojiUri = "file:///C:/project/Contents/Reader.jscript";
    String unicode = Frame(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"initializationOptions":{"projectFile":"C:/project/game.jproject"}}})");
    unicode.Append(Frame(R"({"jsonrpc":"2.0","method":"initialized","params":{}})"));
    String escaped = emojiSource;
    escaped.ReplaceAll("\\", "\\\\").ReplaceAll("\"", "\\\"").ReplaceAll("\n", "\\n");
    unicode.Append(Frame(String(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")").Append(emojiUri.View()).Append(
        R"(","version":1,"text":")").Append(escaped.View()).Append(R"("}}})").View()));
    unicode.Append(Frame(R"({"jsonrpc":"2.0","id":10,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"file:///C:/project/Contents/Reader.jscript"}}})"));
    unicode.Append(Frame(R"({"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":"file:///C:/project/Contents/Reader.jscript"}}})"));
    unicode.Append(Frame(R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})"));
    unicode.Append(Frame(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::istringstream unicodeInput(unicode.Std());
    std::ostringstream unicodeOutput;
    std::ostringstream unicodeLog;
    ServerOptions unicodeOptions;
    unicodeOptions.Files = &files;
    Check(Run(unicodeInput, unicodeOutput, unicodeLog, std::move(unicodeOptions)) == 0,
        "Unicode source protocol session must shut down cleanly");
    const String unicodeResult(unicodeOutput.str());
    Check(unicodeResult.Contains("\"character\":22,\"line\":4"), "diagnostic columns must count emoji as two UTF-16 units");
    Check(unicodeResult.Contains("\"diagnostics\":[],\"uri\":\"file:///C:/project/Contents/Reader.jscript\""),
        "closing an unsaved file must clear its diagnostics");
    TestClientLocaleTags();
    TestScriptSummaryProtocol();
    for (const Bool repair : {false, true})
    {
        String ordered = Frame(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"initializationOptions":{"projectFile":"C:/project/game.jproject"}}})");
        ordered.Append(Frame(R"({"jsonrpc":"2.0","method":"initialized","params":{}})"));
        ordered.Append(Frame(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///C:/project/Contents/Ordered.jscript","version":1,"text":"class C\n{\nInt first = second\nInt second = 2\n}\n"}}})"));
        if (repair)
        {
            ordered.Append(Frame(R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///C:/project/Contents/Ordered.jscript","version":2},"contentChanges":[{"text":"class C\n{\nInt second = 2\nInt first = second\n}\n"}]}})"));
        }
        ordered.Append(Frame(R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})"));
        ordered.Append(Frame(R"({"jsonrpc":"2.0","method":"exit"})"));
        std::istringstream orderedInput(ordered.Std());
        std::ostringstream orderedOutput;
        std::ostringstream orderedLog;
        ServerOptions orderedOptions;
        orderedOptions.Files = &files;
        Check(Run(orderedInput, orderedOutput, orderedLog, std::move(orderedOptions)) == 0,
            "the field initialization diagnostic protocol session shuts down cleanly");
        const String diagnostics(orderedOutput.str());
        if (repair)
        {
            Check(diagnostics.Contains("\"diagnostics\":[],\"uri\":\"file:///C:/project/Contents/Ordered.jscript\",\"version\":2"),
                "reordering fields clears the diagnostic without saving the editor buffer");
        }
        else
        {
            Check(diagnostics.Contains("\"code\":\"JBC3044\"") && diagnostics.Contains("\"character\":12,\"line\":2"),
                "the language server publishes an initialization order error at the original field read");
        }
    }
    std::cout << "  ScriptLanguageServer protocol tests passed\n";
    return 0;
}
