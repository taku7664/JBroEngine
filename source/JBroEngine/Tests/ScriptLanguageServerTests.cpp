#include <JBro/ScriptLanguageServer/Server.h>

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <filesystem>
#include <fstream>

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
    std::cout << "  ScriptLanguageServer protocol tests passed\n";
    return 0;
}
