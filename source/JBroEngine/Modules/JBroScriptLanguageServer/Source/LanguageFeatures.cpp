#include "Session.h"
#include <JBro/ScriptCompiler/Formatting.h>
#include <JBro/ScriptCompiler/Lexer.h>
#include <JBro/Types/Table.h>

#include <algorithm>
#include <functional>

namespace JBro::ScriptLanguageServer::Detail
{
    using namespace ScriptCompiler;

    namespace
    {
        Int32 SymbolNumber(SymbolKind kind)
        {
            switch (kind)
            {
            case SymbolKind::Class:
            case SymbolKind::Type:
                return 5;
            case SymbolKind::Struct:
                return 23;
            case SymbolKind::Interface:
                return 11;
            case SymbolKind::Enum:
                return 10;
            case SymbolKind::EnumMember:
                return 22;
            case SymbolKind::Function:
                return 12;
            case SymbolKind::Method:
                return 6;
            case SymbolKind::Constructor:
                return 9;
            case SymbolKind::Field:
                return 8;
            case SymbolKind::Service:
                return 3;
            default:
                return 13;
            }
        }

        Int32 CompletionNumber(SymbolKind kind)
        {
            switch (kind)
            {
            case SymbolKind::Class:
            case SymbolKind::Type:
                return 7;
            case SymbolKind::Struct:
                return 22;
            case SymbolKind::Interface:
                return 8;
            case SymbolKind::Enum:
                return 13;
            case SymbolKind::EnumMember:
                return 20;
            case SymbolKind::Function:
                return 3;
            case SymbolKind::Method:
                return 2;
            case SymbolKind::Constructor:
                return 4;
            case SymbolKind::Field:
                return 5;
            case SymbolKind::Service:
                return 9;
            default:
                return 6;
            }
        }

        UInt32 TokenNumber(SymbolKind kind)
        {
            switch (kind)
            {
            case SymbolKind::Type:
                return 0;
            case SymbolKind::Class:
                return 1;
            case SymbolKind::Struct:
                return 2;
            case SymbolKind::Interface:
                return 3;
            case SymbolKind::Enum:
                return 4;
            case SymbolKind::EnumMember:
                return 5;
            case SymbolKind::Function:
                return 6;
            case SymbolKind::Method:
            case SymbolKind::Constructor:
                return 7;
            case SymbolKind::Field:
                return 8;
            case SymbolKind::Parameter:
                return 9;
            case SymbolKind::Service:
                return 11;
            default:
                return 10;
            }
        }

        Bool Contains(const SourceRange& range, UInt32 offset)
        {
            return range.Begin.Offset <= offset && offset < range.End.Offset;
        }

        Bool DocumentSymbolKind(SymbolKind kind)
        {
            return kind != SymbolKind::Local && kind != SymbolKind::Parameter;
        }

        String TypeBase(std::string_view type)
        {
            if (type.starts_with("ref "))
            {
                type.remove_prefix(4);
            }
            if (type.starts_with("const "))
            {
                type.remove_prefix(6);
            }
            const std::size_t end = type.find_first_of("?<");
            return String(type.substr(0, end)).Trim();
        }
    }

    const Occurrence* Session::At(const DocumentAnalysis& document, UInt32 offset) const
    {
        const Occurrence* atEnd = nullptr;
        for (const Occurrence& occurrence : document.Occurrences)
        {
            if (Contains(occurrence.Range, offset))
            {
                return &occurrence;
            }
            if (offset == occurrence.Range.End.Offset)
            {
                atEnd = &occurrence;
            }
        }
        return atEnd;
    }

    Json Session::Location(const Symbol& symbol) const
    {
        if (symbol.Uri.IsEmpty())
        {
            return nullptr;
        }
        const DocumentAnalysis* document = FindDocument(*m_analysis, symbol.Uri.View());
        if (nullptr != document)
        {
            return Json{{"uri", symbol.Uri.Std()}, {"range", Range(document->Source.GetText(), symbol.SelectionRange)}};
        }
        String path;
        String source;
        if (UriToPath(symbol.Uri.View(), path) && m_options.Files->Read(path.View(), source))
        {
            return Json{{"uri", symbol.Uri.Std()}, {"range", Range(source.View(), symbol.SelectionRange)}};
        }
        const SourceRange& range = symbol.SelectionRange;
        return Json{{"uri", symbol.Uri.Std()}, {"range", Json{
            {"start", Json{{"line", range.Begin.Line > 0 ? range.Begin.Line - 1 : UInt32(0)}, {"character", range.Begin.Column > 0 ? range.Begin.Column - 1 : UInt32(0)}}},
            {"end", Json{{"line", range.End.Line > 0 ? range.End.Line - 1 : UInt32(0)}, {"character", range.End.Column > 0 ? range.End.Column - 1 : UInt32(0)}}}}}};
    }

    Json Session::Completion(const DocumentAnalysis& document, UInt32 offset)
    {
        Json items = Json::array();
        const Array<SymbolId> candidates = GetCompletionSymbols(*m_analysis, document.Input.Uri.View(), offset);
        UInt32 begin = offset;
        const std::string_view source = document.Source.GetText();
        while (begin > 0)
        {
            const char previous = source[begin - 1];
            if (false == ((previous >= 'a' && previous <= 'z') || (previous >= 'A' && previous <= 'Z') ||
                (previous >= '0' && previous <= '9') || previous == '_'))
            {
                break;
            }
            --begin;
        }
        const std::string_view prefix = source.substr(begin, offset - begin);
        const Json replacement{{"start", Position(source, begin)}, {"end", Position(source, offset)}};
        for (SymbolId id : candidates)
        {
            const Symbol* symbol = FindSymbol(*m_analysis, id);
            if (nullptr == symbol || false == symbol->Name.StartsWith(prefix))
            {
                continue;
            }
            Json item{{"label", symbol->Name.Std()}, {"kind", CompletionNumber(symbol->Kind)},
                {"detail", symbol->Signature.IsEmpty() ? symbol->Type.Std() : symbol->Signature.Std()},
                {"textEdit", Json{{"range", replacement}, {"newText", symbol->Name.Std()}}}};
            if (symbol->Description.IsNotEmpty())
            {
                item["documentation"] = Json{{"kind", m_completionMarkdown ? "markdown" : "plaintext"}, {"value", symbol->Description.Std()}};
            }
            items.push_back(std::move(item));
        }
        UInt32 previous = begin;
        while (previous > 0 && (source[previous - 1] == ' ' || source[previous - 1] == '\t'))
        {
            --previous;
        }
        const Bool member = previous > 0 && source[previous - 1] == '.';
        if (false == member)
        {
            for (const char* keyword : {"script", "class", "struct", "interface", "enum", "fn", "public", "protected", "private", "static", "const", "ref",
                "if", "else", "for", "while", "switch", "case", "default", "break", "continue", "return", "and", "or", "not", "is", "null", "true", "false"})
            {
                if (std::string_view(keyword).starts_with(prefix))
                {
                    items.push_back(Json{{"label", keyword}, {"kind", 14}, {"textEdit", Json{{"range", replacement}, {"newText", keyword}}}});
                }
            }
        }
        return Json{{"isIncomplete", false}, {"items", std::move(items)}};
    }

    Json Session::Hover(const DocumentAnalysis& document, UInt32 offset)
    {
        const Occurrence* occurrence = At(document, offset);
        const Symbol* symbol = nullptr == occurrence ? nullptr : FindSymbol(*m_analysis, occurrence->Target);
        if (nullptr == symbol)
        {
            return nullptr;
        }
        const String signature = symbol->Signature.IsEmpty() ? String(symbol->Type).Append(" ").Append(symbol->Name.View()) : symbol->Signature;
        String description = signature;
        if (m_hoverMarkdown)
        {
            description = String("```jbroscript\n").Append(signature.View()).Append("\n```");
        }
        if (symbol->Description.IsNotEmpty())
        {
            description.Append("\n\n").Append(symbol->Description.View());
        }
        return Json{{"contents", Json{{"kind", m_hoverMarkdown ? "markdown" : "plaintext"}, {"value", description.Std()}}},
            {"range", Range(document.Source.GetText(), occurrence->Range)}};
    }

    Json Session::Definition(const DocumentAnalysis& document, UInt32 offset, Bool type)
    {
        const Occurrence* occurrence = At(document, offset);
        const Symbol* symbol = nullptr == occurrence ? nullptr : FindSymbol(*m_analysis, occurrence->Target);
        if (nullptr == symbol)
        {
            return nullptr;
        }
        if (type)
        {
            const String name = TypeBase(symbol->Type.View());
            const Symbol* found = m_analysis->Symbols.FindBy([&](const Symbol& candidate)
            {
                return candidate.Name == name && (candidate.Kind == SymbolKind::Type || candidate.Kind == SymbolKind::Class ||
                    candidate.Kind == SymbolKind::Struct || candidate.Kind == SymbolKind::Interface || candidate.Kind == SymbolKind::Enum);
            });
            if (nullptr != found)
            {
                symbol = found;
            }
            else if (symbol->Kind != SymbolKind::Class && symbol->Kind != SymbolKind::Struct && symbol->Kind != SymbolKind::Interface &&
                symbol->Kind != SymbolKind::Enum && symbol->Kind != SymbolKind::Type)
            {
                return nullptr;
            }
        }
        return Location(*symbol);
    }

    Json Session::Symbols(const Json& params, Bool workspace)
    {
        Json result = Json::array();
        const String query = JsonString(params, "query").ToLowerCopy();
        const String uri = workspace ? String() : NormalizeUri(JsonString(params.value("textDocument", Json::object()), "uri").View());
        if (workspace || false == m_symbolHierarchy)
        {
            for (const Symbol& symbol : m_analysis->Symbols)
            {
                if (symbol.Api || false == DocumentSymbolKind(symbol.Kind) || (false == workspace && symbol.Uri != uri) ||
                    false == symbol.Name.ToLowerCopy().Contains(query.View()))
                {
                    continue;
                }
                const Json location = Location(symbol);
                if (location.is_null())
                {
                    continue;
                }
                Json item{{"name", symbol.Name.Std()}, {"kind", SymbolNumber(symbol.Kind)}, {"location", location}};
                const Symbol* parent = FindSymbol(*m_analysis, symbol.Parent);
                if (nullptr != parent)
                {
                    item["containerName"] = parent->Name.Std();
                }
                result.push_back(std::move(item));
            }
            return result;
        }
        const DocumentAnalysis* document = FindDocument(*m_analysis, uri.View());
        if (nullptr == document)
        {
            return result;
        }
        std::function<Json(const Symbol&)> build = [&](const Symbol& symbol)
        {
            Json children = Json::array();
            for (const Symbol& child : m_analysis->Symbols)
            {
                if (false == child.Api && child.Parent == symbol.Id && child.Uri == uri && DocumentSymbolKind(child.Kind))
                {
                    children.push_back(build(child));
                }
            }
            return Json{{"name", symbol.Name.Std()}, {"detail", symbol.Signature.Std()}, {"kind", SymbolNumber(symbol.Kind)},
                {"range", Range(document->Source.GetText(), symbol.Range)}, {"selectionRange", Range(document->Source.GetText(), symbol.SelectionRange)},
                {"children", std::move(children)}};
        };
        for (const Symbol& symbol : m_analysis->Symbols)
        {
            if (false == symbol.Api && symbol.Parent == InvalidSymbol && symbol.Uri == uri && DocumentSymbolKind(symbol.Kind))
            {
                result.push_back(build(symbol));
            }
        }
        return result;
    }

    Json Session::Signature(const DocumentAnalysis& document, UInt32 offset)
    {
        const CallSite* selected = nullptr;
        for (const CallSite& call : document.Calls)
        {
            if (offset >= call.CalleeRange.End.Offset && offset <= call.Range.End.Offset &&
                (nullptr == selected || call.Range.End.Offset - call.Range.Begin.Offset < selected->Range.End.Offset - selected->Range.Begin.Offset))
            {
                selected = &call;
            }
        }
        if (nullptr == selected)
        {
            return nullptr;
        }
        Json signatures = Json::array();
        UInt32 activeSignature = 0;
        UInt32 activeParameter = 0;
        for (const SourceRange& argument : selected->Arguments)
        {
            if (offset <= argument.End.Offset)
            {
                break;
            }
            ++activeParameter;
        }
        for (SymbolId id : selected->Overloads)
        {
            const Symbol* symbol = FindSymbol(*m_analysis, id);
            if (nullptr == symbol)
            {
                continue;
            }
            if (id == selected->Resolved)
            {
                activeSignature = static_cast<UInt32>(signatures.size());
            }
            Json parameters = Json::array();
            for (const ParameterInfo& parameter : symbol->Parameters)
            {
                parameters.push_back(Json{{"label", String(parameter.Borrowed ? "ref " : "").Append(parameter.Type.View()).Append(" ").Append(parameter.Name.View()).Std()}});
            }
            Json signature{{"label", symbol->Signature.Std()}, {"parameters", std::move(parameters)}};
            if (symbol->Description.IsNotEmpty())
            {
                signature["documentation"] = Json{{"kind", m_signatureMarkdown ? "markdown" : "plaintext"}, {"value", symbol->Description.Std()}};
            }
            signatures.push_back(std::move(signature));
        }
        if (signatures.empty())
        {
            return nullptr;
        }
        const std::size_t count = signatures[activeSignature]["parameters"].size();
        if (count > 0)
        {
            activeParameter = std::min(activeParameter, static_cast<UInt32>(count - 1));
        }
        return Json{{"signatures", std::move(signatures)}, {"activeSignature", activeSignature}, {"activeParameter", activeParameter}};
    }

    Json Session::References(const DocumentAnalysis& document, UInt32 offset, Bool declarations)
    {
        Json result = Json::array();
        const Occurrence* selected = At(document, offset);
        if (nullptr == selected || selected->Target == InvalidSymbol)
        {
            return result;
        }
        for (const auto& candidate : m_analysis->Documents)
        {
            for (const Occurrence& occurrence : candidate->Occurrences)
            {
                if (occurrence.Target == selected->Target && (declarations || false == occurrence.Declaration))
                {
                    result.push_back(Json{{"uri", candidate->Input.Uri.Std()}, {"range", Range(candidate->Source.GetText(), occurrence.Range)}});
                }
            }
        }
        return result;
    }

    Json Session::Rename(const DocumentAnalysis& document, UInt32 offset, const Json& params, Int32& code, String& error, Bool prepare)
    {
        if (false == m_versionedEdits)
        {
            code = -32601;
            error = "Rename requires the client to support workspaceEdit.documentChanges with document versions.";
            return nullptr;
        }
        const Occurrence* selected = At(document, offset);
        const Array<SymbolId> group = nullptr == selected ? Array<SymbolId>() : GetRenameSymbols(*m_analysis, selected->Target);
        const Symbol* symbol = group.IsEmpty() ? nullptr : FindSymbol(*m_analysis, group.First());
        if (nullptr == symbol)
        {
            if (false == prepare)
            {
                code = -32602;
                error = "Only resolved user symbols can be renamed.";
            }
            return nullptr;
        }
        const String newName = prepare ? symbol->Name : JsonString(params, "newName");
        if (false == CanRenameSymbol(*m_analysis, symbol->Id, newName.View(), error))
        {
            code = -32602;
            return nullptr;
        }
        if (prepare)
        {
            return Json{{"range", Range(document.Source.GetText(), selected->Range)}, {"placeholder", symbol->Name.Std()}};
        }
        Json documentChanges = Json::array();
        for (const auto& candidate : m_analysis->Documents)
        {
            Json edits = Json::array();
            Array<SourceRange> editedRanges;
            for (const Occurrence& occurrence : candidate->Occurrences)
            {
                if (group.Contains(occurrence.Target) && nullptr == editedRanges.FindBy([&](const SourceRange& existing)
                    { return existing.Begin.Offset == occurrence.Range.Begin.Offset && existing.End.Offset == occurrence.Range.End.Offset; }))
                {
                    editedRanges.Add(occurrence.Range);
                    edits.push_back(Json{{"range", Range(candidate->Source.GetText(), occurrence.Range)}, {"newText", newName.Std()}});
                }
            }
            if (false == edits.empty())
            {
                const StoredDocument* stored = Document(candidate->Input.Uri.View());
                const Json version = nullptr != stored && stored->Open ? Json(stored->Input.Version) : Json(nullptr);
                documentChanges.push_back(Json{{"textDocument", Json{{"uri", candidate->Input.Uri.Std()}, {"version", version}}}, {"edits", edits}});
            }
        }
        return Json{{"documentChanges", std::move(documentChanges)}};
    }

    Json Session::Tokens(const DocumentAnalysis& document)
    {
        struct Highlight { UInt32 Begin; UInt32 End; UInt32 Type; UInt32 Modifiers; };
        Array<Highlight> highlights;
        Table<UInt32, const Occurrence*> occurrenceAt;
        for (const Occurrence& occurrence : document.Occurrences)
        {
            occurrenceAt.TryAdd(occurrence.Range.Begin.Offset, &occurrence);
        }
        DiagnosticList ignored;
        const Array<Token> tokens = Lex(document.Source, ignored);
        for (const Token& token : tokens)
        {
            if (token.Kind == TokenKind::EndOfFile || token.Kind == TokenKind::Newline)
            {
                continue;
            }
            UInt32 type = 16;
            UInt32 modifiers = 0;
            if (token.Kind == TokenKind::Identifier)
            {
                const Occurrence* const* found = occurrenceAt.Find(token.Range.Begin.Offset);
                const Occurrence* occurrence = nullptr == found ? nullptr : *found;
                const Symbol* symbol = nullptr == occurrence ? nullptr : FindSymbol(*m_analysis, occurrence->Target);
                type = nullptr == symbol ? UInt32(10) : TokenNumber(symbol->Kind);
                if (nullptr != symbol)
                {
                    modifiers = (occurrence->Declaration ? 1u : 0u) | (false == symbol->Writable ? 2u : 0u) |
                        (symbol->IsStatic ? 4u : 0u) | (symbol->Api ? 8u : 0u);
                }
            }
            else if (token.Kind == TokenKind::StringLiteral)
            {
                type = 13;
            }
            else if (token.Kind == TokenKind::IntegerLiteral || token.Kind == TokenKind::FloatLiteral)
            {
                type = 14;
            }
            else if (token.Kind >= TokenKind::KeywordScript && token.Kind <= TokenKind::KeywordFalse)
            {
                type = 12;
            }
            highlights.Add({token.Range.Begin.Offset, token.Range.End.Offset, type, modifiers});
        }
        const std::string_view text = document.Source.GetText();
        Bool inString = false;
        for (std::size_t index = 0; index + 1 < text.size(); ++index)
        {
            if (inString && text[index] == '\\')
            {
                ++index;
                continue;
            }
            if (text[index] == '"')
            {
                inString = false == inString;
            }
            if (text[index] == '\n')
            {
                inString = false;
            }
            if (false == inString && text[index] == '/' && text[index + 1] == '/')
            {
                std::size_t end = text.find_first_of("\r\n", index);
                if (end == std::string_view::npos)
                {
                    end = text.size();
                }
                highlights.Add({static_cast<UInt32>(index), static_cast<UInt32>(end), 15, 0});
                index = end > 0 ? end - 1 : end;
            }
        }
        std::sort(highlights.begin(), highlights.end(), [](const Highlight& left, const Highlight& right) { return left.Begin < right.Begin; });
        Json data = Json::array();
        UInt32 lastLine = 0;
        UInt32 lastCharacter = 0;
        PositionCursor cursor(text);
        for (const Highlight& highlight : highlights)
        {
            const Json begin = cursor.At(highlight.Begin);
            const Json end = cursor.At(highlight.End);
            const UInt32 line = begin["line"].get<UInt32>();
            const UInt32 character = begin["character"].get<UInt32>();
            const UInt32 endLine = end["line"].get<UInt32>();
            const UInt32 endCharacter = end["character"].get<UInt32>();
            if (line != endLine || endCharacter <= character)
            {
                continue;
            }
            data.push_back(line - lastLine);
            data.push_back(line == lastLine ? character - lastCharacter : character);
            data.push_back(endCharacter - character);
            data.push_back(highlight.Type);
            data.push_back(highlight.Modifiers);
            lastLine = line;
            lastCharacter = character;
        }
        return Json{{"data", std::move(data)}};
    }

    Json Session::Folding(const DocumentAnalysis& document)
    {
        Json result = Json::array();
        Array<UInt64> seen;
        for (NodeIndex index = 0; index < document.Tree.GetNodeCount(); ++index)
        {
            const SyntaxNode& node = document.Tree.Get(index);
            if (node.Kind != SyntaxKind::Block && node.Kind != SyntaxKind::ScriptDeclaration && node.Kind != SyntaxKind::ClassDeclaration &&
                node.Kind != SyntaxKind::StructDeclaration && node.Kind != SyntaxKind::InterfaceDeclaration && node.Kind != SyntaxKind::EnumDeclaration)
            {
                continue;
            }
            const Json begin = Position(document.Source.GetText(), node.Range.Begin.Offset);
            const Json end = Position(document.Source.GetText(), node.Range.End.Offset);
            const UInt32 startLine = begin["line"].get<UInt32>();
            const UInt32 endLine = end["line"].get<UInt32>();
            const UInt64 key = (static_cast<UInt64>(startLine) << 32) | endLine;
            if (endLine > startLine && false == seen.Contains(key))
            {
                seen.Add(key);
                result.push_back(Json{{"startLine", startLine}, {"startCharacter", begin["character"]}, {"endLine", endLine},
                    {"endCharacter", end["character"]}, {"kind", "region"}});
            }
        }
        return result;
    }

    Json Session::Formatting(const DocumentAnalysis& document, const Json& params)
    {
        const Json options = params.value("options", Json::object());
        const UInt32 tabSize = options.value("tabSize", 4u);
        const Bool spaces = options.value("insertSpaces", true);
        String formatted;
        if (false == FormatSource(document.Source, tabSize, spaces, formatted))
        {
            return Json::array();
        }
        if (formatted.View() == document.Source.GetText())
        {
            return Json::array();
        }
        return Json::array({Json{{"range", Json{{"start", Position(document.Source.GetText(), 0)},
            {"end", Position(document.Source.GetText(), static_cast<UInt32>(document.Source.GetText().size()))}}}, {"newText", formatted.Std()}}});
    }

    Json Session::Request(std::string_view method, const Json& params, Int32& code, String& error)
    {
        Analyze();
        if (method == "workspace/symbol")
        {
            return Symbols(params, true);
        }
        const String uri = NormalizeUri(JsonString(params.value("textDocument", Json::object()), "uri").View());
        const DocumentAnalysis* document = FindDocument(*m_analysis, uri.View());
        if (nullptr == document)
        {
            code = -32602;
            error = "The requested document is not part of this workspace.";
            return nullptr;
        }
        if (method == "textDocument/documentSymbol")
        {
            return Symbols(params, false);
        }
        if (method == "textDocument/semanticTokens/full")
        {
            return Tokens(*document);
        }
        if (method == "textDocument/foldingRange")
        {
            return Folding(*document);
        }
        if (method == "textDocument/formatting")
        {
            return Formatting(*document, params);
        }
        UInt32 offset = 0;
        if (false == params.contains("position") || false == Offset(document->Source.GetText(), params["position"], offset))
        {
            code = -32602;
            error = "A valid UTF-16 position is required.";
            return nullptr;
        }
        if (method == "textDocument/completion")
        {
            return Completion(*document, offset);
        }
        if (method == "textDocument/hover")
        {
            return Hover(*document, offset);
        }
        if (method == "textDocument/definition" || method == "textDocument/typeDefinition")
        {
            return Definition(*document, offset, method == "textDocument/typeDefinition");
        }
        if (method == "textDocument/signatureHelp")
        {
            return Signature(*document, offset);
        }
        if (method == "textDocument/references")
        {
            return References(*document, offset, params.value("context", Json::object()).value("includeDeclaration", false));
        }
        if (method == "textDocument/rename" || method == "textDocument/prepareRename")
        {
            return Rename(*document, offset, params, code, error, method == "textDocument/prepareRename");
        }
        code = -32601;
        error = "Method not found.";
        return nullptr;
    }
}
