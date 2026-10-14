#include "Session.h"
#include <JBro/Core/Yaml.h>
#include <JBro/Core/Version.h>

#include <algorithm>

namespace JBro::ScriptLanguageServer::Detail
{
    using namespace ScriptCompiler;

    Session::Session(ServerOptions options) : m_options(std::move(options))
    {
    }

    Bool Session::ReadProject(const Json& params, String& error)
    {
        if (nullptr == m_options.Files)
        {
            error = "The executable host did not provide workspace file access.";
            return false;
        }
        const Json initialization = params.value("initializationOptions", Json::object());
        const String specified = JsonString(initialization, "projectFile");
        if (specified.IsEmpty())
        {
            error = "initializationOptions.projectFile must explicitly name the active .jproject.";
            return false;
        }
        String projectPath;
        if (false == UriToPath(specified.View(), projectPath))
        {
            projectPath = specified;
        }
        m_projectFile = m_options.Files->Resolve({}, projectPath.View());
        if (false == m_projectFile.ToLowerCopy().EndsWith(".jproject"))
        {
            error = "projectFile must name a .jproject file.";
            return false;
        }
        String source;
        if (false == m_options.Files->Read(m_projectFile.View(), source))
        {
            error = String("Cannot read project: ").Append(m_projectFile.View());
            return false;
        }
        YamlDocument project;
        YamlError yamlError;
        if (false == project.Parse(source.data(), source.size(), yamlError) || project.GetKind(project.GetRoot()) != YamlKind::Map)
        {
            error = String("Invalid project YAML: ").Append(yamlError.message.View());
            return false;
        }
        String dimension;
        if (false == project.FindScalar(project.GetRoot(), "Framework", dimension) || dimension.ToUpperCopy() != "2D")
        {
            error = "JBroScript language service currently requires Framework: 2D; 3D is unsupported.";
            return false;
        }
        String engineVersion;
        if (false == project.FindScalar(project.GetRoot(), "EngineVersion", engineVersion) || engineVersion.IsEmpty())
        {
            error = "The project must specify EngineVersion.";
            return false;
        }
        if (engineVersion != EngineVersionText)
        {
            error = String("Project EngineVersion '").Append(engineVersion.View()).Append("' does not match this language server's engine catalog '")
                .Append(EngineVersionText).Append("'.");
            return false;
        }
        String root = ".";
        project.FindScalar(project.GetRoot(), "RootPath", root);
        String directory = "Contents";
        project.FindScalar(project.GetRoot(), "ScriptSourceDirectory", directory);
        const String rootDirectory = m_options.Files->Resolve(m_projectFile.View(), root.View());
        const String rootAnchor = String(rootDirectory).Append("/.__jbro_lsp_root__");
        m_sourceDirectory = m_options.Files->Resolve(rootAnchor.View(), directory.View());
        return RefreshFiles(error);
    }

    Bool Session::Initialize(const Json& params, String& error)
    {
        String requestedLocale = JsonString(params, "locale", m_options.Locale.View());
        if (requestedLocale.IsEmpty())
        {
            requestedLocale = m_options.Locale;
        }
        const String language = requestedLocale.ToLowerCopy();
        // LSP clients send language tags such as `ko`; the bundled tables use regional names.
        const char* locale = language == "ko" || language.StartsWith("ko-") ? "ko-KR" : "en-US";
        if (m_options.LocalizationDirectory.IsNotEmpty())
        {
            if (false == m_messages.Load(m_options.LocalizationDirectory.c_str(), locale, "en-US"))
            {
                m_messages.Load(m_options.LocalizationDirectory.c_str(), "en-US", nullptr);
            }
        }
        const Json capabilities = params.value("capabilities", Json::object());
        const Json textDocument = capabilities.value("textDocument", Json::object());
        const auto markdown = [](const Json& formats) -> Bool
        {
            if (formats.is_array())
            {
                for (const Json& format : formats)
                {
                    if (format == "markdown")
                    {
                        return true;
                    }
                    if (format == "plaintext")
                    {
                        return false;
                    }
                }
            }
            return false;
        };
        m_hoverMarkdown = markdown(textDocument.value("hover", Json::object()).value("contentFormat", Json::array()));
        m_completionMarkdown = markdown(textDocument.value("completion", Json::object()).value("completionItem", Json::object())
            .value("documentationFormat", Json::array()));
        m_signatureMarkdown = markdown(textDocument.value("signatureHelp", Json::object()).value("signatureInformation", Json::object())
            .value("documentationFormat", Json::array()));
        const Json symbols = textDocument.value("documentSymbol", Json::object());
        m_symbolHierarchy = symbols.value("hierarchicalDocumentSymbolSupport", false);
        const Json workspace = capabilities.value("workspace", Json::object());
        const Json edit = workspace.value("workspaceEdit", Json::object());
        m_versionedEdits = edit.value("documentChanges", false);
        return ReadProject(params, error);
    }

    StoredDocument* Session::MutableDocument(std::string_view uri)
    {
        const String normalized = NormalizeUri(uri);
        return m_documents.FindBy([&](const StoredDocument& document) { return document.Input.Uri == normalized; });
    }

    const StoredDocument* Session::Document(std::string_view uri) const
    {
        const String normalized = NormalizeUri(uri);
        return m_documents.FindBy([&](const StoredDocument& document) { return document.Input.Uri == normalized; });
    }

    Json Session::WatchOptions(Bool relativePatterns) const
    {
        Json pattern;
        if (relativePatterns)
        {
            pattern = Json{{"baseUri", PathToUri(m_sourceDirectory.View()).Std()}, {"pattern", "**/*.jscript"}};
        }
        else
        {
            String absolute = m_sourceDirectory;
            absolute.ReplaceAll("\\", "/");
            if (false == absolute.EndsWith("/"))
            {
                absolute += '/';
            }
            pattern = absolute.Append("**/*.jscript").Std();
        }
        return Json{{"watchers", Json::array({Json{{"globPattern", std::move(pattern)}, {"kind", 7}}})}};
    }

    Bool Session::RefreshFiles(String& error)
    {
        Array<String> paths;
        if (false == m_options.Files->EnumerateScripts(m_sourceDirectory.View(), paths, error))
        {
            return false;
        }
        Array<StoredDocument> refreshed;
        for (const StoredDocument& document : m_documents)
        {
            if (document.Open)
            {
                StoredDocument copy = document;
                copy.OnDisk = false;
                refreshed.Add(std::move(copy));
            }
        }
        for (const String& path : paths)
        {
            const String uri = NormalizeUri(PathToUri(path.View()).View());
            StoredDocument* document = refreshed.FindBy([&](const StoredDocument& item) { return item.Input.Uri == uri; });
            if (nullptr == document)
            {
                StoredDocument created;
                created.Input.Uri = uri;
                created.Input.Path = path;
                document = &refreshed.Add(std::move(created));
            }
            document->OnDisk = true;
            if (false == document->Open)
            {
                String source;
                if (false == m_options.Files->Read(path.View(), source))
                {
                    error = String("Cannot read script: ").Append(path.View());
                    return false;
                }
                document->Input.Text = std::move(source);
            }
        }
        for (const StoredDocument& document : m_documents)
        {
            if (nullptr == refreshed.FindBy([&](const StoredDocument& item) { return item.Input.Uri == document.Input.Uri; }))
            {
                ClearedUris.Add(document.Input.Uri);
            }
        }
        m_documents = std::move(refreshed);
        Dirty = true;
        return true;
    }

    Bool Session::ChangeDocument(std::string_view method, const Json& params, String& error)
    {
        if (method == "workspace/didChangeWatchedFiles" || method == "workspace/didCreateFiles" ||
            method == "workspace/didDeleteFiles" || method == "workspace/didRenameFiles")
        {
            return RefreshFiles(error);
        }
        if (false == params.contains("textDocument") || false == params["textDocument"].is_object())
        {
            error = "A textDocument object is required.";
            return false;
        }
        const Json& textDocument = params["textDocument"];
        const String uri = NormalizeUri(JsonString(textDocument, "uri").View());
        StoredDocument* document = MutableDocument(uri.View());
        if (method == "textDocument/didOpen")
        {
            if (false == textDocument.contains("text") || false == textDocument["text"].is_string() ||
                false == textDocument.contains("version") || false == textDocument["version"].is_number_integer())
            {
                error = "didOpen requires text and an integer version.";
                return false;
            }
            String path;
            if (false == UriToPath(uri.View(), path) || false == path.ToLowerCopy().EndsWith(".jscript"))
            {
                error = "Only file URIs naming .jscript documents are supported.";
                return false;
            }
            const String resolved = m_options.Files->Resolve(m_projectFile.View(), path.View());
            const String normalizedPath = PathToUri(resolved.View()).ToLowerCopy();
            String sourcePrefix = PathToUri(m_sourceDirectory.View()).ToLowerCopy();
            if (false == sourcePrefix.EndsWith("/"))
            {
                sourcePrefix += '/';
            }
            if (false == normalizedPath.StartsWith(sourcePrefix.View()))
            {
                error = "The document must be inside this project's ScriptSourceDirectory.";
                return false;
            }
            if (nullptr != document && document->Open)
            {
                error = "The document is already open.";
                return false;
            }
            if (nullptr == document)
            {
                StoredDocument created;
                created.Input.Uri = uri;
                created.Input.Path = path;
                document = &m_documents.Add(std::move(created));
            }
            document->Open = true;
            document->Input.Text = JsonString(textDocument, "text");
            document->Input.Version = textDocument["version"].get<Int64>();
        }
        else if (method == "textDocument/didChange")
        {
            if (nullptr == document || false == document->Open || false == textDocument.contains("version") ||
                false == textDocument["version"].is_number_integer() || false == params.contains("contentChanges") ||
                false == params["contentChanges"].is_array())
            {
                error = "didChange requires an open document, integer version and contentChanges array.";
                return false;
            }
            const Int64 version = textDocument["version"].get<Int64>();
            if (version <= document->Input.Version)
            {
                return true;
            }
            String source = document->Input.Text;
            for (const Json& change : params["contentChanges"])
            {
                if (false == change.is_object() || false == change.contains("text") || false == change["text"].is_string())
                {
                    error = "Every content change must contain text.";
                    return false;
                }
                const String replacement = JsonString(change, "text");
                if (change.contains("range"))
                {
                    const Json& range = change["range"];
                    UInt32 begin = 0;
                    UInt32 end = 0;
                    if (false == range.is_object() || false == range.contains("start") || false == range.contains("end") ||
                        false == Offset(source.View(), range["start"], begin) || false == Offset(source.View(), range["end"], end) || begin > end)
                    {
                        error = "The incremental edit range is invalid (positions use UTF-16).";
                        return false;
                    }
                    source.replace(begin, end - begin, replacement);
                }
                else
                {
                    source = replacement;
                }
            }
            document->Input.Text = std::move(source);
            document->Input.Version = version;
        }
        else if (method == "textDocument/didClose")
        {
            if (nullptr == document || false == document->Open)
            {
                return true;
            }
            document->Open = false;
            document->Input.Version = 0;
            ClearedUris.Add(uri);
            String source;
            if (m_options.Files->Read(document->Input.Path.View(), source))
            {
                document->Input.Text = std::move(source);
                document->OnDisk = true;
            }
            else
            {
                m_documents.RemoveAll([&](const StoredDocument& item) { return item.Input.Uri == uri; });
            }
        }
        else if (method == "textDocument/didSave")
        {
            // didSave does not replace an open document's in-memory text or write anything to disk.
            return RefreshFiles(error);
        }
        Dirty = true;
        return true;
    }

    const AnalysisResult& Session::Analyze()
    {
        if (Dirty || nullptr == m_analysis)
        {
            Array<DocumentInput> inputs;
            inputs.Reserve(m_documents.Size());
            for (const StoredDocument& document : m_documents)
            {
                inputs.Add(document.Input);
            }
            m_analysis = nullptr;
            m_analysis = &m_analyzer.Update(inputs.View(), m_options.Catalog);
            Dirty = false;
        }
        return *m_analysis;
    }

    Array<Json> Session::Diagnostics()
    {
        Array<Json> result;
        for (const String& uri : ClearedUris)
        {
            result.Add(Json{{"uri", uri.Std()}, {"diagnostics", Json::array()}});
        }
        const AnalysisResult& analysis = Analyze();
        for (const auto& document : analysis.Documents)
        {
            const StoredDocument* stored = Document(document->Input.Uri.View());
            if (nullptr == stored || false == stored->Open)
            {
                continue;
            }
            Json diagnostics = Json::array();
            for (const Diagnostic& item : document->ParseDiagnostics.GetItems())
            {
                diagnostics.push_back(Json{{"range", Range(document->Source.GetText(), item.Range)}, {"severity", 1},
                    {"code", String("JBC").Append(std::to_string(GetDiagnosticNumber(item.Code))).Std()},
                    {"source", "JBroScript"}, {"message", m_messages.Format(item).Std()}});
            }
            for (const SemanticDiagnostic& item : document->Diagnostics)
            {
                const String message = m_messages.FormatKey(item.MessageKey.c_str(), item.Arguments.View());
                diagnostics.push_back(Json{{"range", Range(document->Source.GetText(), item.Range)}, {"severity", item.Warning ? 2 : 1},
                    {"code", item.Code.Std()}, {"source", "JBroScript"}, {"message", message.Std()}});
            }
            result.Add(Json{{"uri", document->Input.Uri.Std()}, {"version", document->Input.Version}, {"diagnostics", std::move(diagnostics)}});
        }
        return result;
    }
}
