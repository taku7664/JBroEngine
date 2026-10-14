#pragma once

#include <JBro/ScriptCompiler/ApiCatalog.h>
#include <JBro/ScriptCompiler/Diagnostic.h>
#include <JBro/ScriptCompiler/SyntaxTree.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/Int.h>

namespace JBro::ScriptCompiler
{
    using SymbolId = UInt32;
    inline constexpr SymbolId InvalidSymbol = static_cast<SymbolId>(-1);

    enum class SymbolKind : unsigned char
    {
        Type, Class, Struct, Interface, Enum, EnumMember, Function, Method,
        Constructor, Field, Parameter, Local, Service
    };

    struct DocumentInput
    {
        String Uri;
        String Path;
        String Text;
        Int64 Version = 0;
    };

    struct Symbol
    {
        SymbolId Id = InvalidSymbol;
        SymbolId Parent = InvalidSymbol;
        SymbolKind Kind = SymbolKind::Local;
        String Name;
        String Type;
        String Signature;
        String Description;
        String Uri;
        SourceRange Range;
        SourceRange SelectionRange;
        SourceRange ScopeRange;
        Array<ParameterInfo> Parameters;
        Array<String> TypeParameters;
        Array<String> BaseTypes;
        Bool Api = false;
        Bool Writable = true;
        Bool IsStatic = false;
        Bool IsCallback = false;
        Bool IsPrivate = false;
        Bool IsProtected = false;
        Bool IsAbstract = false;
        Bool MutatesReceiver = false;
        Bool MayInvalidateHandles = false;
        Bool HasOutputProjection = false;
    };

    struct Occurrence
    {
        SymbolId Target = InvalidSymbol;
        SourceRange Range;
        Bool Declaration = false;
    };

    struct CallSite
    {
        SourceRange Range;
        SourceRange CalleeRange;
        Array<SourceRange> Arguments;
        Array<SymbolId> Overloads;
        SymbolId Resolved = InvalidSymbol;
    };

    struct SemanticDiagnostic
    {
        String Code;
        String MessageKey;
        SourceRange Range;
        Array<String> Arguments;
        Bool Warning = false;
    };

    struct DocumentAnalysis
    {
        explicit DocumentAnalysis(const DocumentInput& input)
            : Input(input), Source(input.Path, input.Text)
        {
        }
        DocumentInput Input;
        SourceText Source;
        SyntaxTree Tree;
        DiagnosticList ParseDiagnostics;
        Array<SemanticDiagnostic> Diagnostics;
        Array<Occurrence> Occurrences;
        Array<CallSite> Calls;
        std::size_t DeclarationDiagnosticCount = 0;
        std::size_t DeclarationOccurrenceCount = 0;
    };

    struct AnalysisResult
    {
        Array<OwnerPtr<DocumentAnalysis>> Documents;
        Array<Symbol> Symbols;
        std::size_t ParsedDocuments = 0;
        std::size_t CheckedDocuments = 0;
    };

    OwnerPtr<AnalysisResult> AnalyzeProject(ArrayView<const DocumentInput> documents, const ApiCatalog& api);
    OwnerPtr<AnalysisResult> AnalyzeProjectIncremental(ArrayView<const DocumentInput> documents,
        const ApiCatalog& api, OwnerPtr<AnalysisResult> previous);
    const Symbol* FindSymbol(const AnalysisResult& result, SymbolId id);
    const DocumentAnalysis* FindDocument(const AnalysisResult& result, std::string_view uri);
    Array<SymbolId> GetCompletionSymbols(const AnalysisResult& result, std::string_view uri, UInt32 offset);
    // Returns the connected rename contract: class/constructors or matching overrides/requirements.
    // Class IDs precede their constructors. API/callback members in a method group make it immutable.
    Array<SymbolId> GetRenameSymbols(const AnalysisResult& result, SymbolId id);
    Bool CanRenameSymbol(const AnalysisResult& result, SymbolId id, std::string_view newName, String& error);
    ArrayView<const char* const> GetSemanticDiagnosticKeys();

    class AnalysisSession final
    {
    public:
        const AnalysisResult& Update(ArrayView<const DocumentInput> documents, const ApiCatalog& api);
        const AnalysisResult* GetResult() const noexcept { return m_result.Get(); }

    private:
        OwnerPtr<AnalysisResult> m_result;
        String m_apiSignature;
    };
}
