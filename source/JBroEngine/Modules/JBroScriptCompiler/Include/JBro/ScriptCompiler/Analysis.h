#pragma once

#include <JBro/ScriptCompiler/ApiCatalog.h>
#include <JBro/ScriptCompiler/Diagnostic.h>
#include <JBro/ScriptCompiler/SyntaxTree.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/Table.h>
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
        // 생성기가 그 투영을 내릴 수 있는가(`CanEmitOutputProjection`). 거짓이면 호출이 `JBC3050` 이다(D-382).
        Bool ProjectionEmittable = true;
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
        String ResultType;
        Array<String> ParameterTypes; // Resolved destination types for supplied arguments.
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
        // 이름으로 찾는 색인이다(D-387). 분석이 심볼을 더할 때(`Symbols` 끝에 붙일 때)만 함께 늘고, 심볼의 Name·Type·Parent·Kind 는
        // 더한 뒤 바뀌지 않는다. 전에는 타입·멤버를 찾을 때마다 전체 심볼(엔진 API 수천 개 포함)을 훑어 필드 수의 제곱으로 느렸다.
        // 타입 심볼의 `Type` → 처음 더한 그 타입.
        Table<String, SymbolId> TypeIndex;
        // `Parent` → 자식 심볼들(더한 차례). 최상위는 `InvalidSymbol` 아래에 있다.
        Table<SymbolId, Array<SymbolId>> ChildIndex;
        // `MemberKey(Parent, Name)` → 그 이름의 자식들(더한 차례). 큰 타입에서 이름 하나를 찾을 때 자식 전체를 훑지 않는다.
        Table<String, Array<SymbolId>> MemberIndex;
        std::size_t ParsedDocuments = 0;
        std::size_t CheckedDocuments = 0;
    };

    // `AnalysisResult::MemberIndex` 의 키다.
    String MemberKey(SymbolId parent, std::string_view name);
    OwnerPtr<AnalysisResult> AnalyzeProject(ArrayView<const DocumentInput> documents, const ApiCatalog& api);
    OwnerPtr<AnalysisResult> AnalyzeProjectIncremental(ArrayView<const DocumentInput> documents,
        const ApiCatalog& api, OwnerPtr<AnalysisResult> previous);
    const Symbol* FindSymbol(const AnalysisResult& result, SymbolId id);
    const DocumentAnalysis* FindDocument(const AnalysisResult& result, std::string_view uri);
    // The analyzed type of one syntax expression, including selected call overloads.
    String GetExpressionType(const AnalysisResult& result, const DocumentAnalysis& document, NodeIndex index);
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
