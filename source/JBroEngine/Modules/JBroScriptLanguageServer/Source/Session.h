#pragma once

#include "TextCoordinates.h"
#include <JBro/ScriptLanguageServer/Server.h>
#include <JBro/ScriptCompiler/Analysis.h>
#include <JBro/ScriptCompiler/DiagnosticMessages.h>

namespace JBro::ScriptLanguageServer::Detail
{
    struct StoredDocument
    {
        ScriptCompiler::DocumentInput Input;
        Bool Open = false;
        Bool OnDisk = false;
    };

    class Session final
    {
    public:
        explicit Session(ServerOptions options);
        Bool Initialize(const Json& params, String& error);
        Bool ChangeDocument(std::string_view method, const Json& params, String& error);
        Bool RefreshFiles(String& error);
        const ScriptCompiler::AnalysisResult& Analyze();
        Array<Json> Diagnostics();
        Json Request(std::string_view method, const Json& params, Int32& code, String& error);
        const StoredDocument* Document(std::string_view uri) const;
        Json WatchOptions(Bool relativePatterns) const;
        Bool SupportsVersionedEdits() const noexcept { return m_versionedEdits; }
        Bool Dirty = true;
        Array<String> ClearedUris;

    private:
        ServerOptions m_options;
        String m_projectFile;
        String m_sourceDirectory;
        Array<StoredDocument> m_documents;
        ScriptCompiler::AnalysisSession m_analyzer;
        const ScriptCompiler::AnalysisResult* m_analysis = nullptr;
        ScriptCompiler::DiagnosticMessages m_messages;
        Bool m_versionedEdits = false;
        Bool m_symbolHierarchy = true;
        Bool m_hoverMarkdown = false;
        Bool m_completionMarkdown = false;
        Bool m_signatureMarkdown = false;
        Bool ReadProject(const Json& params, String& error);
        StoredDocument* MutableDocument(std::string_view uri);
        Json Location(const ScriptCompiler::Symbol& symbol) const;
        const ScriptCompiler::Occurrence* At(const ScriptCompiler::DocumentAnalysis& document, UInt32 offset) const;
        Json Completion(const ScriptCompiler::DocumentAnalysis& document, UInt32 offset);
        Json Hover(const ScriptCompiler::DocumentAnalysis& document, UInt32 offset);
        Json Definition(const ScriptCompiler::DocumentAnalysis& document, UInt32 offset, Bool type);
        Json Symbols(const Json& params, Bool workspace);
        Json Signature(const ScriptCompiler::DocumentAnalysis& document, UInt32 offset);
        Json References(const ScriptCompiler::DocumentAnalysis& document, UInt32 offset, Bool declarations);
        Json Rename(const ScriptCompiler::DocumentAnalysis& document, UInt32 offset, const Json& params, Int32& code, String& error, Bool prepare);
        Json Tokens(const ScriptCompiler::DocumentAnalysis& document);
        Json Folding(const ScriptCompiler::DocumentAnalysis& document);
        Json Formatting(const ScriptCompiler::DocumentAnalysis& document, const Json& params);
    };
}
