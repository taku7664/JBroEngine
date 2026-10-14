#pragma once

#include <JBro/ScriptCompiler/ApiCatalog.h>
#include <JBro/Types/Int.h>

#include <iosfwd>

namespace JBro::ScriptLanguageServer
{
    // File access belongs to the executable host, not the language-service module.
    class WorkspaceFiles
    {
    public:
        virtual ~WorkspaceFiles() = default;
        virtual Bool Read(std::string_view path, String& text) = 0;
        // Resolve path relative to the directory containing baseFile; return an absolute UTF-8 path.
        virtual String Resolve(std::string_view baseFile, std::string_view path) = 0;
        virtual Bool EnumerateScripts(std::string_view directory, Array<String>& paths, String& error) = 0;
    };

    struct ServerOptions
    {
        ScriptCompiler::ApiCatalog Catalog;
        WorkspaceFiles* Files = nullptr;
        String LocalizationDirectory;
        String Locale = "ko-KR";
    };

    // Streams carry binary Content-Length framed JSON-RPC. Only log receives operational text.
    Int32 Run(std::istream& input, std::ostream& output, std::ostream& log, ServerOptions options);
    Int32 RunStdio(ServerOptions options);
}
