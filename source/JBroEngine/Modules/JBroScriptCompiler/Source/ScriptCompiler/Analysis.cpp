#include <JBro/ScriptCompiler/Analysis.h>
#include <JBro/ScriptCompiler/Parser.h>
#include <JBro/ScriptCompiler/Lexer.h>
#include <JBro/Types/Table.h>

#include <algorithm>
#include <charconv>
#include <limits>
#include <utility>

namespace JBro::ScriptCompiler
{
    namespace
    {
        Bool Nullable(std::string_view type)
        {
            if (type.starts_with("const "))
            {
                type.remove_prefix(6);
            }
            return type.starts_with("ref ");
        }

        String Bare(std::string_view type)
        {
            if (type.starts_with("const "))
            {
                type.remove_prefix(6);
            }
            if (type.starts_with("ref "))
            {
                type.remove_prefix(4);
            }
            if (type.starts_with("const "))
            {
                type.remove_prefix(6);
            }
            return String(type);
        }

        String RefType(std::string_view type)
        {
            return Nullable(type) ? String(type) : String("ref ").Append(type);
        }

        String TypeBase(std::string_view type)
        {
            String value = Bare(type);
            const std::size_t angle = value.find('<');
            if (String::npos != angle)
            {
                value.resize(angle);
            }
            return value;
        }

        Array<String> TypeArguments(std::string_view type)
        {
            Array<String> result;
            const std::size_t begin = type.find('<');
            const std::size_t end = type.rfind('>');
            if (std::string_view::npos == begin || std::string_view::npos == end || begin >= end)
            {
                return result;
            }
            std::size_t argument = begin + 1;
            std::size_t depth = 0;
            for (std::size_t index = argument; index <= end; ++index)
            {
                if ((index == end || ',' == type[index]) && 0 == depth)
                {
                    result.Add(String(type.substr(argument, index - argument)).Trimmed());
                    argument = index + 1;
                }
                else if ('<' == type[index])
                {
                    ++depth;
                }
                else if ('>' == type[index] && 0 != depth)
                {
                    --depth;
                }
            }
            return result;
        }

        Bool IsTypeSymbol(SymbolKind kind)
        {
            return SymbolKind::Type == kind || SymbolKind::Class == kind || SymbolKind::Struct == kind
                || SymbolKind::Interface == kind || SymbolKind::Enum == kind;
        }

        Bool IsFunction(SymbolKind kind)
        {
            return SymbolKind::Function == kind || SymbolKind::Method == kind || SymbolKind::Constructor == kind;
        }

        Bool EqualType(std::string_view left, std::string_view right)
        {
            const auto canonical = [](std::string_view type)
            {
                String value;
                for (char c : type)
                {
                    if (' ' != c && '\t' != c && '\r' != c && '\n' != c)
                    {
                        value.push_back(c);
                    }
                }
                return value;
            };
            return canonical(left) == canonical(right);
        }

        Bool Contains(const SourceRange& range, UInt32 offset)
        {
            return range.Begin.Offset <= offset && offset <= range.End.Offset;
        }

        SymbolId TypeId(const AnalysisResult& result, std::string_view type)
        {
            const String name = TypeBase(type);
            for (const Symbol& symbol : result.Symbols)
            {
                if (IsTypeSymbol(symbol.Kind) && symbol.Name == name)
                {
                    return symbol.Id;
                }
            }
            return InvalidSymbol;
        }

        Bool Derives(const AnalysisResult& result, SymbolId type, SymbolId base, Array<SymbolId>& visited)
        {
            if (type == base && InvalidSymbol != type)
            {
                return true;
            }
            if (InvalidSymbol == type || visited.Contains(type))
            {
                return false;
            }
            visited.Add(type);
            for (const String& parent : result.Symbols[type].BaseTypes)
            {
                if (Derives(result, TypeId(result, parent), base, visited))
                {
                    return true;
                }
            }
            return false;
        }

        Bool Derives(const AnalysisResult& result, SymbolId type, SymbolId base)
        {
            Array<SymbolId> visited;
            return Derives(result, type, base, visited);
        }

        Bool Accessible(const AnalysisResult& result, const Symbol& member, SymbolId owner)
        {
            if (member.IsPrivate)
            {
                return member.Parent == owner;
            }
            if (member.IsProtected)
            {
                return Derives(result, owner, member.Parent);
            }
            return true;
        }

        Bool SameParameters(const Symbol& left, const Symbol& right)
        {
            if (left.Parameters.Size() != right.Parameters.Size())
            {
                return false;
            }
            for (std::size_t index = 0; index < left.Parameters.Size(); ++index)
            {
                if (false == EqualType(left.Parameters[index].Type, right.Parameters[index].Type)
                    || left.Parameters[index].Borrowed != right.Parameters[index].Borrowed)
                {
                    return false;
                }
            }
            return true;
        }

        void Members(const AnalysisResult& result, SymbolId type, std::string_view name,
            Array<SymbolId>& output, Array<SymbolId>& visited)
        {
            if (InvalidSymbol == type || visited.Contains(type))
            {
                return;
            }
            visited.Add(type);
            for (const Symbol& symbol : result.Symbols)
            {
                if (symbol.Parent != type || (false == name.empty() && symbol.Name != name))
                {
                    continue;
                }
                const Bool hidden = nullptr != output.FindBy([&](SymbolId id)
                {
                    const Symbol& earlier = result.Symbols[id];
                    return earlier.Name == symbol.Name && (false == IsFunction(symbol.Kind)
                        || (IsFunction(earlier.Kind) && SameParameters(earlier, symbol)));
                });
                if (false == hidden)
                {
                    output.Add(symbol.Id);
                }
            }
            for (const String& base : result.Symbols[type].BaseTypes)
            {
                Members(result, TypeId(result, base), name, output, visited);
            }
        }

        Array<SymbolId> Members(const AnalysisResult& result, SymbolId type, std::string_view name = {})
        {
            Array<SymbolId> output;
            Array<SymbolId> visited;
            Members(result, type, name, output, visited);
            return output;
        }

        String Substitute(std::string_view type, const Array<String>& names, const Array<String>& values)
        {
            String result;
            std::size_t index = 0;
            while (index < type.size())
            {
                const std::size_t begin = index;
                const char c = type[index];
                if (('_' == c) || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z'))
                {
                    ++index;
                    while (index < type.size() && (('_' == type[index]) || ('a' <= type[index] && type[index] <= 'z')
                        || ('A' <= type[index] && type[index] <= 'Z') || ('0' <= type[index] && type[index] <= '9')))
                    {
                        ++index;
                    }
                    const std::string_view word = type.substr(begin, index - begin);
                    Bool replaced = false;
                    for (std::size_t slot = 0; slot < names.Size() && slot < values.Size(); ++slot)
                    {
                        if (names[slot] == word)
                        {
                            result.Append(values[slot]);
                            replaced = true;
                            break;
                        }
                    }
                    if (false == replaced)
                    {
                        result.Append(word);
                    }
                }
                else
                {
                    result.push_back(c);
                    ++index;
                }
            }
            while (result.Contains("ref ref "))
            {
                result.ReplaceAll("ref ref ", "ref ");
            }
            return result;
        }

        SourceLocation Location(const SourceText& source, std::size_t offset)
        {
            const std::string_view text = source.GetText();
            offset = std::min(offset, text.size());
            SourceLocation result;
            result.Offset = static_cast<UInt32>(offset);
            std::size_t lineStart = text.starts_with("\xEF\xBB\xBF") ? 3 : 0;
            for (std::size_t index = 0; index < offset; ++index)
            {
                if ('\n' == text[index])
                {
                    ++result.Line;
                    lineStart = index + 1;
                }
            }
            result.Column = static_cast<UInt32>(offset >= lineStart ? offset - lineStart + 1 : 1);
            return result;
        }

        SourceRange NameRange(const DocumentAnalysis& document, const SyntaxNode& node)
        {
            if (node.Text.empty())
            {
                return node.Range;
            }
            const std::string_view source = document.Source.GetText();
            const std::size_t offset = static_cast<std::size_t>(node.Text.data() - source.data());
            return {Location(document.Source, offset), Location(document.Source, offset + node.Text.size())};
        }

        String RenderType(const SyntaxTree& tree, NodeIndex index)
        {
            if (InvalidNode == index)
            {
                return "Void";
            }
            const SyntaxNode& node = tree.Get(index);
            String type(node.Text);
            const auto arguments = tree.GetChildren(index);
            if (0 != arguments.Size())
            {
                type.Append("<");
                Bool first = true;
                for (NodeIndex argument : arguments)
                {
                    if (false == first)
                    {
                        type.Append(", ");
                    }
                    type.Append(RenderType(tree, argument));
                    first = false;
                }
                type.Append(">");
            }
            if (0 != (node.Flags & (SyntaxFlagRef | SyntaxFlagNullable)))
            {
                type = RefType(type);
            }
            return type;
        }

        String Signature(const Symbol& symbol)
        {
            if (false == IsFunction(symbol.Kind))
            {
                return String(symbol.Type).Append(" ").Append(symbol.Name);
            }
            String value("fn ");
            value.Append(symbol.Name).Append("(");
            Bool first = true;
            for (const ParameterInfo& parameter : symbol.Parameters)
            {
                if (false == first)
                {
                    value.Append(", ");
                }
                if (parameter.Borrowed)
                {
                    value.Append("ref ");
                }
                value.Append(parameter.Type).Append(" ").Append(parameter.Name);
                if (parameter.HasDefault)
                {
                    value.Append(" = ").Append(parameter.DefaultValue);
                }
                first = false;
            }
            value.Append(")");
            if (symbol.Type != "Void")
            {
                value.Append(" -> ").Append(symbol.Type);
            }
            return value;
        }

        void AppendDeclarationShape(const SyntaxTree& tree, NodeIndex index, String& output)
        {
            if (InvalidNode == index)
            {
                output.Append("missing;");
                return;
            }
            const SyntaxNode& node = tree.Get(index);
            output.Append(GetSyntaxKindName(node.Kind)).Append(":").Append(std::to_string(node.Flags))
                .Append(":").Append(std::to_string(node.Text.size())).Append(":").Append(node.Text).Append(";");
            if (SyntaxKind::FunctionDeclaration == node.Kind)
            {
                AppendDeclarationShape(tree, tree.GetChild(index, 1), output);
                AppendDeclarationShape(tree, tree.GetChild(index, 2), output);
            }
            else if (SyntaxKind::FieldDeclaration == node.Kind)
            {
                AppendDeclarationShape(tree, tree.GetChild(index, 1), output);
            }
            else
            {
                for (NodeIndex child : tree.GetChildren(index))
                {
                    AppendDeclarationShape(tree, child, output);
                }
            }
        }

        String DeclarationShape(const DocumentAnalysis& document)
        {
            String shape;
            AppendDeclarationShape(document.Tree, document.Tree.GetRoot(), shape);
            return shape;
        }

        String ReceiverSubstitution(std::string_view type, std::string_view receiver)
        {
            const Array<String> arguments = TypeArguments(receiver);
            if (TypeBase(receiver) == "Array")
            {
                return Substitute(type, {"T"}, arguments);
            }
            if (TypeBase(receiver) == "Table")
            {
                return Substitute(type, {"K", "V"}, arguments);
            }
            return String(type);
        }

        String ExpressionType(const AnalysisResult& result, const DocumentAnalysis& document, NodeIndex index)
        {
            if (InvalidNode == index)
            {
                return {};
            }
            const SyntaxTree& tree = document.Tree;
            const SyntaxNode& node = tree.Get(index);
            switch (node.Kind)
            {
            case SyntaxKind::IntegerLiteral:
                return "Int";
            case SyntaxKind::FloatLiteral:
                return "Float";
            case SyntaxKind::StringLiteral:
                return "String";
            case SyntaxKind::BooleanLiteral:
                return "Bool";
            case SyntaxKind::NameExpression:
            case SyntaxKind::GenericNameExpression:
            {
                const SourceRange range = NameRange(document, node);
                for (const Occurrence& occurrence : document.Occurrences)
                {
                    if (occurrence.Range.Begin.Offset == range.Begin.Offset && occurrence.Range.End.Offset == range.End.Offset)
                    {
                        return result.Symbols[occurrence.Target].Type;
                    }
                }
                if (node.Text == "this")
                {
                    for (const Symbol& symbol : result.Symbols)
                    {
                        if (IsTypeSymbol(symbol.Kind) && symbol.Uri == document.Input.Uri && Contains(symbol.Range, node.Range.Begin.Offset))
                        {
                            return symbol.Name;
                        }
                    }
                }
                return {};
            }
            case SyntaxKind::MemberAccessExpression:
            {
                const String receiver = ExpressionType(result, document, tree.GetChild(index, 0));
                const Array<SymbolId> members = Members(result, TypeId(result, receiver), node.Text);
                return members.IsEmpty() ? String() : ReceiverSubstitution(result.Symbols[members[0]].Type, receiver);
            }
            case SyntaxKind::CallExpression:
            {
                for (const CallSite& call : document.Calls)
                {
                    if (call.Range.Begin.Offset == node.Range.Begin.Offset && call.Range.End.Offset == node.Range.End.Offset
                        && InvalidSymbol != call.Resolved)
                    {
                        const Symbol& function = result.Symbols[call.Resolved];
                        const NodeIndex callee = tree.GetChild(index, 0);
                        const SyntaxNode& calleeNode = tree.Get(callee);
                        const Bool member = SyntaxKind::MemberAccessExpression == calleeNode.Kind;
                        const String receiver = member ? ExpressionType(result, document, tree.GetChild(callee, 0)) : String();
                        Array<String> explicitTypes;
                        for (std::size_t slot = member ? 1 : 0; slot < calleeNode.ChildCount; ++slot)
                        {
                            explicitTypes.Add(RenderType(tree, tree.GetChild(callee, slot)));
                        }
                        return Substitute(ReceiverSubstitution(function.Type, receiver), function.TypeParameters, explicitTypes);
                    }
                }
                return {};
            }
            case SyntaxKind::IndexExpression:
            {
                const String receiver = ExpressionType(result, document, tree.GetChild(index, 0));
                const Array<String> arguments = TypeArguments(receiver);
                if (TypeBase(receiver) == "Array" && arguments.Size() == 1)
                {
                    return arguments[0];
                }
                if (TypeBase(receiver) == "Table" && arguments.Size() == 2)
                {
                    return arguments[1];
                }
                return TypeBase(receiver) == "String" ? String("String") : String();
            }
            case SyntaxKind::ParenthesizedExpression:
            case SyntaxKind::RefArgument:
            case SyntaxKind::UnaryExpression:
                return ExpressionType(result, document, tree.GetChild(index, 0));
            case SyntaxKind::NullAssertExpression:
                return Bare(ExpressionType(result, document, tree.GetChild(index, 0)));
            case SyntaxKind::BinaryExpression:
                return node.Text == "??" ? Bare(ExpressionType(result, document, tree.GetChild(index, 0)))
                    : ExpressionType(result, document, tree.GetChild(index, 0));
            default:
                return {};
            }
        }

        struct Binding
        {
            DocumentAnalysis* Document = nullptr;
            NodeIndex Node = InvalidNode;
            SymbolId Id = InvalidSymbol;
        };

        struct Scope
        {
            SourceRange Range;
            Array<SymbolId> Symbols;
        };

        struct Flow
        {
            Array<SymbolId> Initialized;
            Array<String> NonNull;
        };

        struct ControlFlow
        {
            Bool FallsThrough = true;
            Bool Returns = false;
            Bool Breaks = false;
            Bool Continues = false;
            Flow BreakFlow;
            Flow ContinueFlow;

            Bool AlwaysReturns() const
            {
                return Returns && false == FallsThrough && false == Breaks && false == Continues;
            }
        };

        struct Expression
        {
            String Type;
            String DeclaredType;
            SymbolId Target = InvalidSymbol;
            Array<SymbolId> Overloads;
            String Path;
            String ReceiverType;
            Bool LValue = false;
            Bool Writable = false;
            Bool Borrowed = false;
            Bool TypeObject = false;
            Bool StaticReceiver = false;
            Bool NullSafe = false;
        };

        struct ReusedBody
        {
            DocumentAnalysis* Document = nullptr;
            Array<SemanticDiagnostic> Diagnostics;
            Array<Occurrence> Occurrences;
            Array<CallSite> Calls;
        };

        class Analyzer final
        {
        public:
            Analyzer(AnalysisResult& result, const ApiCatalog& api,
                const AnalysisResult* previous = nullptr, Array<String> reused = {})
                : m_result(result), m_api(api), m_previous(previous), m_reused(std::move(reused))
            {
            }

            void Run()
            {
                for (auto& document : m_result.Documents)
                {
                    if (m_reused.Contains(document->Input.Uri))
                    {
                        ReusedBody body;
                        body.Document = document.Get();
                        for (std::size_t index = document->DeclarationDiagnosticCount; index < document->Diagnostics.Size(); ++index)
                        {
                            body.Diagnostics.Add(document->Diagnostics[index]);
                        }
                        for (std::size_t index = document->DeclarationOccurrenceCount; index < document->Occurrences.Size(); ++index)
                        {
                            body.Occurrences.Add(document->Occurrences[index]);
                        }
                        body.Calls = std::move(document->Calls);
                        m_reusedBodies.Add(std::move(body));
                    }
                    document->Diagnostics.Clear();
                    document->Occurrences.Clear();
                    document->Calls.Clear();
                }
                AddApi();
                CollectTypes();
                CollectMembers();
                ValidateDeclarations();
                for (auto& document : m_result.Documents)
                {
                    document->DeclarationDiagnosticCount = document->Diagnostics.Size();
                    document->DeclarationOccurrenceCount = document->Occurrences.Size();
                }
                RestoreBodies();
                for (const Binding& binding : m_bindings)
                {
                    if (m_reused.Contains(binding.Document->Input.Uri))
                    {
                        continue;
                    }
                    if (IsTypeSymbol(m_result.Symbols[binding.Id].Kind))
                    {
                        continue;
                    }
                    m_document = binding.Document;
                    m_owner = m_result.Symbols[binding.Id].Parent;
                    m_function = InvalidSymbol;
                    Flow flow;
                    if (SyntaxKind::FieldDeclaration == Node(binding.Node).Kind)
                    {
                        const NodeIndex initializer = Child(binding.Node, 2);
                        if (InvalidNode != initializer)
                        {
                            const Expression value = Expr(initializer, flow);
                            CheckAssignable(m_result.Symbols[binding.Id].Type, value, Node(initializer).Range);
                        }
                    }
                    else if (SyntaxKind::FunctionDeclaration == Node(binding.Node).Kind)
                    {
                        AnalyzeFunction(binding, flow);
                    }
                }
            }

        private:
            void RestoreBodies()
            {
                if (nullptr == m_previous)
                {
                    return;
                }
                Array<SymbolId> remap(m_previous->Symbols.Size());
                for (SymbolId& id : remap)
                {
                    id = InvalidSymbol;
                }
                for (const Symbol& old : m_previous->Symbols)
                {
                    if (SymbolKind::Local == old.Kind || SymbolKind::Parameter == old.Kind)
                    {
                        continue;
                    }
                    const Symbol* match = m_result.Symbols.FindBy([&](const Symbol& candidate)
                    {
                        if (candidate.Name != old.Name || candidate.Kind != old.Kind || candidate.Uri != old.Uri)
                        {
                            return false;
                        }
                        if (InvalidSymbol == old.Parent || InvalidSymbol == candidate.Parent)
                        {
                            return old.Parent == candidate.Parent;
                        }
                        return m_previous->Symbols[old.Parent].Name == m_result.Symbols[candidate.Parent].Name
                            && SameParameters(old, candidate);
                    });
                    if (nullptr != match)
                    {
                        remap[old.Id] = match->Id;
                    }
                }
                for (const Symbol& old : m_previous->Symbols)
                {
                    if ((SymbolKind::Local == old.Kind || SymbolKind::Parameter == old.Kind)
                        && m_reused.Contains(old.Uri) && InvalidSymbol != old.Parent && InvalidSymbol != remap[old.Parent])
                    {
                        Symbol restored = old;
                        restored.Parent = remap[old.Parent];
                        remap[old.Id] = Add(std::move(restored));
                    }
                }
                for (ReusedBody& body : m_reusedBodies)
                {
                    m_document = body.Document;
                    for (const SemanticDiagnostic& diagnostic : body.Diagnostics)
                    {
                        m_document->Diagnostics.Add(diagnostic);
                    }
                    for (Occurrence occurrence : body.Occurrences)
                    {
                        if (InvalidSymbol != occurrence.Target && occurrence.Target < remap.Size())
                        {
                            occurrence.Target = remap[occurrence.Target];
                            if (InvalidSymbol != occurrence.Target)
                            {
                                m_document->Occurrences.Add(std::move(occurrence));
                            }
                        }
                    }
                    for (CallSite call : body.Calls)
                    {
                        if (InvalidSymbol != call.Resolved)
                        {
                            call.Resolved = remap[call.Resolved];
                        }
                        for (SymbolId& overload : call.Overloads)
                        {
                            overload = remap[overload];
                        }
                        call.Overloads.RemoveAll([](SymbolId id) { return InvalidSymbol == id; });
                        m_document->Calls.Add(std::move(call));
                    }
                }
            }

            const SyntaxNode& Node(NodeIndex index) const
            {
                return m_document->Tree.Get(index);
            }

            NodeIndex Child(NodeIndex index, std::size_t slot) const
            {
                return m_document->Tree.GetChild(index, slot);
            }

            void Error(const char* code, const char* key, const SourceRange& range,
                std::initializer_list<String> arguments = {}, Bool warning = false)
            {
                SemanticDiagnostic& diagnostic = m_document->Diagnostics.Emplace();
                diagnostic.Code = code;
                diagnostic.MessageKey = String("jbroc.semantic.").Append(key);
                diagnostic.Range = range;
                for (const String& argument : arguments)
                {
                    diagnostic.Arguments.Add(argument);
                }
                diagnostic.Warning = warning;
            }

            SymbolId Add(Symbol symbol)
            {
                symbol.Id = static_cast<SymbolId>(m_result.Symbols.Size());
                symbol.Signature = Signature(symbol);
                const SymbolId id = symbol.Id;
                m_result.Symbols.Add(std::move(symbol));
                return id;
            }

            void Occur(SymbolId target, const SourceRange& range, Bool declaration = false)
            {
                if (InvalidSymbol == target)
                {
                    return;
                }
                const Bool exists = nullptr != m_document->Occurrences.FindBy([&](const Occurrence& occurrence)
                {
                    return occurrence.Range.Begin.Offset == range.Begin.Offset && occurrence.Range.End.Offset == range.End.Offset
                        && occurrence.Target == target;
                });
                if (false == exists)
                {
                    m_document->Occurrences.Add({target, range, declaration});
                }
            }

            void AddApi()
            {
                for (const char* primitive : {"Int", "Float", "Bool", "String", "Void", "Array", "Table"})
                {
                    Symbol symbol;
                    symbol.Name = primitive;
                    symbol.Type = primitive;
                    symbol.Kind = SymbolKind::Type;
                    symbol.Api = true;
                    Add(std::move(symbol));
                }
                for (const ApiType& type : m_api.Types)
                {
                    SymbolId id = TypeId(m_result, type.Name);
                    if (InvalidSymbol == id)
                    {
                        Symbol symbol;
                        symbol.Name = type.Name;
                        symbol.Type = type.Name;
                        symbol.Api = true;
                        symbol.Uri = type.DefinitionUri;
                        symbol.Range = type.DefinitionRange;
                        symbol.SelectionRange = type.DefinitionRange;
                        symbol.BaseTypes = type.Bases;
                        switch (type.Kind)
                        {
                        case ApiTypeKind::Class: symbol.Kind = SymbolKind::Class; break;
                        case ApiTypeKind::Interface: symbol.Kind = SymbolKind::Interface; break;
                        case ApiTypeKind::Enum: symbol.Kind = SymbolKind::Enum; break;
                        default: symbol.Kind = SymbolKind::Type; break;
                        }
                        id = Add(std::move(symbol));
                    }
                    else
                    {
                        m_result.Symbols[id].BaseTypes = type.Bases;
                        m_result.Symbols[id].Uri = type.DefinitionUri;
                    }
                    if (ApiTypeKind::Handle == type.Kind || ApiTypeKind::Script == type.Kind)
                    {
                        m_handles.Add(id);
                    }
                }
                for (const ApiType& type : m_api.Types)
                {
                    const SymbolId parent = TypeId(m_result, type.Name);
                    for (const ApiMember& member : type.Members)
                    {
                        Symbol symbol;
                        symbol.Parent = parent;
                        symbol.Name = member.Name;
                        symbol.Type = member.Type.IsEmpty() ? String("Void") : member.Type;
                        symbol.Kind = ApiMemberKind::Field == member.Kind ? SymbolKind::Field
                            : (ApiMemberKind::Constructor == member.Kind ? SymbolKind::Constructor : SymbolKind::Method);
                        symbol.Parameters = member.Parameters;
                        symbol.TypeParameters = member.TypeParameters;
                        symbol.Api = true;
                        symbol.Writable = member.Writable;
                        symbol.IsStatic = member.IsStatic;
                        symbol.IsCallback = member.IsCallback;
                        symbol.MutatesReceiver = member.MutatesReceiver;
                        symbol.MayInvalidateHandles = member.MayInvalidateHandles;
                        symbol.HasOutputProjection = member.HasOutputProjection;
                        symbol.Description = member.Description;
                        symbol.Uri = member.DefinitionUri;
                        symbol.Range = member.DefinitionRange;
                        symbol.SelectionRange = member.DefinitionRange;
                        Add(std::move(symbol));
                    }
                    for (const String& name : type.EnumValues)
                    {
                        Symbol symbol;
                        symbol.Parent = parent;
                        symbol.Name = name;
                        symbol.Type = type.Name;
                        symbol.Kind = SymbolKind::EnumMember;
                        symbol.Api = true;
                        symbol.Writable = false;
                        symbol.IsStatic = true;
                        Add(std::move(symbol));
                    }
                }
                for (const ApiGlobal& global : m_api.Globals)
                {
                    Symbol symbol;
                    symbol.Name = global.Name;
                    symbol.Type = global.Type;
                    symbol.Kind = SymbolKind::Service;
                    symbol.Api = true;
                    symbol.Writable = false;
                    Add(std::move(symbol));
                }
                for (const ApiMember& function : m_api.Functions)
                {
                    Symbol symbol;
                    symbol.Kind = SymbolKind::Function;
                    symbol.Name = function.Name;
                    symbol.Type = function.Type.IsEmpty() ? String("Void") : function.Type;
                    symbol.Parameters = function.Parameters;
                    symbol.TypeParameters = function.TypeParameters;
                    symbol.Api = true;
                    symbol.IsStatic = true;
                    symbol.MutatesReceiver = function.MutatesReceiver;
                    symbol.MayInvalidateHandles = function.MayInvalidateHandles;
                    symbol.HasOutputProjection = function.HasOutputProjection;
                    symbol.Description = function.Description;
                    symbol.Uri = function.DefinitionUri;
                    symbol.Range = function.DefinitionRange;
                    symbol.SelectionRange = function.DefinitionRange;
                    Add(std::move(symbol));
                }
            }

            Symbol FromNode(NodeIndex index, SymbolId parent, SymbolKind kind)
            {
                const SyntaxNode& node = Node(index);
                Symbol symbol;
                symbol.Parent = parent;
                symbol.Kind = kind;
                symbol.Name = String(node.Text);
                symbol.Uri = m_document->Input.Uri;
                symbol.Range = node.Range;
                symbol.SelectionRange = NameRange(*m_document, node);
                symbol.ScopeRange = InvalidSymbol == parent ? m_document->Tree.Get(m_document->Tree.GetRoot()).Range : m_result.Symbols[parent].Range;
                symbol.IsStatic = 0 != (node.Flags & SyntaxFlagStatic);
                symbol.IsCallback = 0 != (node.Flags & SyntaxFlagCallback);
                symbol.IsAbstract = 0 != (node.Flags & SyntaxFlagRequire);
                const Bool publicDefault = InvalidSymbol != parent && (SymbolKind::Struct == m_result.Symbols[parent].Kind
                    || SymbolKind::Interface == m_result.Symbols[parent].Kind);
                symbol.IsPrivate = 0 != (node.Flags & SyntaxFlagPrivate) || (false == publicDefault
                    && 0 == (node.Flags & (SyntaxFlagPublic | SyntaxFlagProtected)));
                symbol.IsProtected = 0 != (node.Flags & SyntaxFlagProtected);
                return symbol;
            }

            void CollectTypes()
            {
                for (auto& document : m_result.Documents)
                {
                    m_document = document.Get();
                    for (NodeIndex declaration : m_document->Tree.GetChildren(m_document->Tree.GetRoot()))
                    {
                        const SyntaxNode& node = Node(declaration);
                        if (node.Text.empty())
                        {
                            continue;
                        }
                        SymbolKind kind = SymbolKind::Class;
                        switch (node.Kind)
                        {
                        case SyntaxKind::StructDeclaration: kind = SymbolKind::Struct; break;
                        case SyntaxKind::InterfaceDeclaration: kind = SymbolKind::Interface; break;
                        case SyntaxKind::EnumDeclaration: kind = SymbolKind::Enum; break;
                        default: break;
                        }
                        if (InvalidSymbol != TypeId(m_result, node.Text))
                        {
                            Error("JBC3002", "duplicate", NameRange(*m_document, node), {String(node.Text)});
                        }
                        Symbol symbol = FromNode(declaration, InvalidSymbol, kind);
                        symbol.Type = symbol.Name;
                        symbol.IsPrivate = false;
                        const SymbolId id = Add(std::move(symbol));
                        m_bindings.Add({m_document, declaration, id});
                        if (SyntaxKind::ScriptDeclaration == node.Kind)
                        {
                            m_handles.Add(id);
                            m_scripts.Add(id);
                        }
                        Occur(id, m_result.Symbols[id].SelectionRange, true);
                    }
                }
            }

            void CollectMembers()
            {
                const std::size_t typeBindings = m_bindings.Size();
                for (std::size_t bindingIndex = 0; bindingIndex < typeBindings; ++bindingIndex)
                {
                    const Binding binding = m_bindings[bindingIndex];
                    m_document = binding.Document;
                    for (NodeIndex child : m_document->Tree.GetChildren(binding.Node))
                    {
                        if (InvalidNode == child)
                        {
                            continue;
                        }
                        if (SyntaxKind::BaseList == Node(child).Kind)
                        {
                            for (NodeIndex base : m_document->Tree.GetChildren(child))
                            {
                                m_result.Symbols[binding.Id].BaseTypes.Add(RenderType(m_document->Tree, base));
                            }
                            continue;
                        }
                        SymbolKind kind = SymbolKind::Field;
                        if (SyntaxKind::FunctionDeclaration == Node(child).Kind)
                        {
                            kind = Node(child).Text == m_result.Symbols[binding.Id].Name ? SymbolKind::Constructor : SymbolKind::Method;
                        }
                        else if (SyntaxKind::EnumMember == Node(child).Kind)
                        {
                            kind = SymbolKind::EnumMember;
                        }
                        Symbol symbol = FromNode(child, binding.Id, kind);
                        if (IsFunction(kind))
                        {
                            symbol.Type = RenderType(m_document->Tree, Child(child, 2));
                            const NodeIndex parameters = Child(child, 1);
                            if (InvalidNode != parameters)
                            {
                                for (NodeIndex parameter : m_document->Tree.GetChildren(parameters))
                                {
                                    const NodeIndex type = Child(parameter, 0);
                                    ParameterInfo info;
                                    info.Name = String(Node(parameter).Text);
                                    info.Type = RenderType(m_document->Tree, type);
                                    info.Borrowed = InvalidNode != type && 0 != (Node(type).Flags & SyntaxFlagRef);
                                    if (info.Borrowed && 0 != (Node(type).Flags & SyntaxFlagConst))
                                    {
                                        info.Type = String("const ").Append(info.Type);
                                    }
                                    symbol.Parameters.Add(std::move(info));
                                }
                            }
                            symbol.IsAbstract |= SymbolKind::Interface == m_result.Symbols[binding.Id].Kind;
                            symbol.MutatesReceiver = false == symbol.IsStatic;
                            symbol.MayInvalidateHandles = true;
                            if (SymbolKind::Constructor == kind)
                            {
                                symbol.Type = m_result.Symbols[binding.Id].Name;
                            }
                        }
                        else if (SymbolKind::EnumMember == kind)
                        {
                            symbol.Type = m_result.Symbols[binding.Id].Name;
                            symbol.Writable = false;
                            symbol.IsStatic = true;
                            symbol.IsPrivate = false;
                        }
                        else
                        {
                            const NodeIndex type = Child(child, 1);
                            symbol.Type = RenderType(m_document->Tree, type);
                            symbol.Writable = InvalidNode == type || 0 == (Node(type).Flags & SyntaxFlagConst);
                        }
                        const SymbolId id = Add(std::move(symbol));
                        m_bindings.Add({m_document, child, id});
                        Occur(id, m_result.Symbols[id].SelectionRange, true);
                        if (SymbolKind::Constructor == kind)
                        {
                            Occur(binding.Id, m_result.Symbols[id].SelectionRange, true);
                        }
                    }
                }
                for (SymbolId script : m_scripts)
                {
                    Bool hasScriptBase = false;
                    for (const String& base : m_result.Symbols[script].BaseTypes)
                    {
                        hasScriptBase |= m_scripts.Contains(TypeId(m_result, base))
                            || Derives(m_result, TypeId(m_result, base), TypeId(m_result, "GameScriptBase"))
                            || base == "GameScript2D";
                    }
                    if (false == hasScriptBase && InvalidSymbol != TypeId(m_result, "GameScript2D"))
                    {
                        m_result.Symbols[script].BaseTypes.Add("GameScript2D");
                    }
                }
            }

            void ValidateType(NodeIndex index, Bool borrowAllowed, Bool returnType = false, Bool descriptor = false)
            {
                if (InvalidNode == index)
                {
                    return;
                }
                const SyntaxNode& node = Node(index);
                const SymbolId id = TypeId(m_result, node.Text);
                if (InvalidSymbol == id)
                {
                    Error("JBC3001", "unknown_type", NameRange(*m_document, node), {String(node.Text)});
                }
                else
                {
                    Occur(id, NameRange(*m_document, node));
                    if (false == descriptor && m_handles.Contains(id)
                        && 0 == (node.Flags & (SyntaxFlagRef | SyntaxFlagNullable)))
                    {
                        Error("JBC3009", "handle_nullable", node.Range, {String(node.Text)});
                    }
                    if (0 != (node.Flags & SyntaxFlagNullable))
                    {
                        Error("JBC3040", "legacy_nullable_type", node.Range, {String(node.Text)});
                    }
                    if ((SymbolKind::Interface == m_result.Symbols[id].Kind && false == borrowAllowed)
                        || (false == returnType && node.Text == "Void"))
                    {
                        Error("JBC3020", "invalid_value_type", node.Range, {String(node.Text)});
                    }
                    if (false == descriptor && false == borrowAllowed && false == returnType && Abstract(id)
                        && 0 == (node.Flags & (SyntaxFlagRef | SyntaxFlagNullable)))
                    {
                        Error("JBC3016", "abstract_value", node.Range, {String(node.Text)});
                    }
                }
                const auto children = m_document->Tree.GetChildren(index);
                const std::size_t requiredArguments = node.Text == "Array" ? 1 : (node.Text == "Table" ? 2 : 0);
                if (children.Size() != requiredArguments)
                {
                    Error("JBC3021", "type_arguments", node.Range, {String(node.Text)});
                }
                for (NodeIndex child : children)
                {
                    ValidateType(child, false);
                }
                if (node.Text == "Table" && children.Size() == 2)
                {
                    const String key = RenderType(m_document->Tree, children[0]);
                    if (key != "Int" && key != "String")
                    {
                        Error("JBC3022", "table_key", Node(children[0]).Range);
                    }
                }
            }

            void Ancestors(SymbolId type, Table<SymbolId, UInt32>& counts, Array<SymbolId>& path, Bool& cycle)
            {
                if (InvalidSymbol == type)
                {
                    return;
                }
                if (path.Contains(type))
                {
                    cycle = true;
                    return;
                }
                ++counts[type];
                path.Add(type);
                for (const String& base : m_result.Symbols[type].BaseTypes)
                {
                    Ancestors(TypeId(m_result, base), counts, path, cycle);
                }
                path.Pop();
            }

            void ValidateDeclarations()
            {
                for (const Binding& binding : m_bindings)
                {
                    m_document = binding.Document;
                    m_owner = m_result.Symbols[binding.Id].Parent;
                    const Symbol symbol = m_result.Symbols[binding.Id];
                    const SyntaxNode& node = Node(binding.Node);
                    for (const Symbol& earlier : m_result.Symbols)
                    {
                        if (earlier.Id >= symbol.Id || earlier.Parent != symbol.Parent || earlier.Name != symbol.Name)
                        {
                            continue;
                        }
                        if (IsFunction(symbol.Kind) && IsFunction(earlier.Kind) && false == SameParameters(symbol, earlier))
                        {
                            continue;
                        }
                        if (IsTypeSymbol(symbol.Kind))
                        {
                            break;
                        }
                        Error("JBC3002", "duplicate", symbol.SelectionRange, {symbol.Name});
                        break;
                    }
                    if (IsTypeSymbol(symbol.Kind))
                    {
                        const NodeIndex bases = Child(binding.Node, 0);
                        if (InvalidNode != bases && SyntaxKind::BaseList == Node(bases).Kind)
                        {
                            for (NodeIndex base : m_document->Tree.GetChildren(bases))
                            {
                                const SymbolId baseId = TypeId(m_result, Node(base).Text);
                                if (InvalidSymbol == baseId)
                                {
                                    Error("JBC3001", "unknown_type", Node(base).Range, {String(Node(base).Text)});
                                }
                                else
                                {
                                    Occur(baseId, NameRange(*m_document, Node(base)));
                                    if (SymbolKind::Struct == m_result.Symbols[baseId].Kind || SymbolKind::Enum == m_result.Symbols[baseId].Kind)
                                    {
                                        Error("JBC3023", "invalid_base", Node(base).Range, {String(Node(base).Text)});
                                    }
                                }
                            }
                        }
                        Table<SymbolId, UInt32> counts;
                        Array<SymbolId> path;
                        Bool cycle = false;
                        Ancestors(symbol.Id, counts, path, cycle);
                        if (cycle)
                        {
                            Error("JBC3014", "inheritance_cycle", symbol.SelectionRange, {symbol.Name});
                        }
                        for (const auto& count : counts)
                        {
                            if (count.MappedValue > 1 && SymbolKind::Interface != m_result.Symbols[count.KeyValue].Kind)
                            {
                                Error("JBC3015", "inheritance_diamond", symbol.SelectionRange, {symbol.Name, m_result.Symbols[count.KeyValue].Name});
                                break;
                            }
                        }
                        continue;
                    }
                    if (SymbolKind::Field == symbol.Kind)
                    {
                        ValidateType(Child(binding.Node, 1), false);
                        if (SymbolKind::Interface == m_result.Symbols[symbol.Parent].Kind)
                        {
                            Error("JBC3024", "interface_field", symbol.SelectionRange);
                        }
                        continue;
                    }
                    if (false == IsFunction(symbol.Kind))
                    {
                        continue;
                    }
                    const NodeIndex parameters = Child(binding.Node, 1);
                    if (InvalidNode != parameters)
                    {
                        for (NodeIndex parameter : m_document->Tree.GetChildren(parameters))
                        {
                            ValidateType(Child(parameter, 0), true);
                        }
                    }
                    ValidateType(Child(binding.Node, 2), false, true);
                    if (SymbolKind::Constructor == symbol.Kind && (SymbolKind::Class != m_result.Symbols[symbol.Parent].Kind
                        || m_scripts.Contains(symbol.Parent)))
                    {
                        Error("JBC3025", "constructor_kind", symbol.SelectionRange);
                    }
                    if (symbol.IsAbstract && InvalidNode != Child(binding.Node, 3))
                    {
                        Error("JBC3026", "abstract_body", symbol.SelectionRange);
                    }
                    if (false == symbol.IsAbstract && InvalidNode == Child(binding.Node, 3))
                    {
                        Error("JBC3027", "missing_body", symbol.SelectionRange);
                    }
                    Array<SymbolId> baseMembers;
                    for (const String& base : m_result.Symbols[symbol.Parent].BaseTypes)
                    {
                        const Array<SymbolId> members = Members(m_result, TypeId(m_result, base), symbol.Name);
                        baseMembers.Append(members.Data(), members.Size());
                    }
                    const SymbolId* matching = baseMembers.FindBy([&](SymbolId id)
                    {
                        return IsFunction(m_result.Symbols[id].Kind) && SameParameters(symbol, m_result.Symbols[id])
                            && EqualType(symbol.Type, m_result.Symbols[id].Type);
                    });
                    if (symbol.IsCallback && (nullptr == matching || false == m_result.Symbols[*matching].Api
                        || false == m_result.Symbols[*matching].IsCallback || false == m_scripts.Contains(symbol.Parent)))
                    {
                        Error("JBC3012", "callback", symbol.SelectionRange, {symbol.Name});
                    }
                    if (0 != (node.Flags & SyntaxFlagOverride) && (nullptr == matching || m_result.Symbols[*matching].IsPrivate))
                    {
                        Error("JBC3013", "override", symbol.SelectionRange, {symbol.Name});
                    }
                }
            }

            Bool Abstract(SymbolId type) const
            {
                const Array<SymbolId> members = Members(m_result, type);
                return nullptr != members.FindBy([&](SymbolId id) { return m_result.Symbols[id].IsAbstract; });
            }

            void AnalyzeFunction(const Binding& binding, Flow& flow)
            {
                m_function = binding.Id;
                m_scopes.Clear();
                const NodeIndex body = Child(binding.Node, 3);
                if (InvalidNode == body)
                {
                    return;
                }
                m_scopes.Add({Node(body).Range, {}});
                const NodeIndex parameters = Child(binding.Node, 1);
                if (InvalidNode != parameters)
                {
                    for (NodeIndex parameter : m_document->Tree.GetChildren(parameters))
                    {
                        const NodeIndex type = Child(parameter, 0);
                        if (InvalidNode == type)
                        {
                            continue;
                        }
                        Symbol symbol = FromNode(parameter, m_function, SymbolKind::Parameter);
                        symbol.Type = RenderType(m_document->Tree, type);
                        symbol.ScopeRange = Node(body).Range;
                        symbol.Writable = 0 == (Node(type).Flags & SyntaxFlagConst);
                        flow.Initialized.Add(AddLocal(std::move(symbol)));
                    }
                }
                m_loopDepth = 0;
                m_switchDepth = 0;
                const ControlFlow completion = Block(body, flow, false);
                if (false == completion.AlwaysReturns() && m_result.Symbols[m_function].Type != "Void"
                    && SymbolKind::Constructor != m_result.Symbols[m_function].Kind)
                {
                    Error("JBC3011", "return_path", m_result.Symbols[m_function].SelectionRange,
                        {m_result.Symbols[m_function].Name});
                }
                m_scopes.Clear();
            }

            SymbolId AddLocal(Symbol symbol)
            {
                symbol.IsPrivate = false;
                symbol.IsProtected = false;
                for (SymbolId existing : m_scopes.Last().Symbols)
                {
                    if (m_result.Symbols[existing].Name == symbol.Name)
                    {
                        Error("JBC3002", "duplicate", symbol.SelectionRange, {symbol.Name});
                        break;
                    }
                }
                const SymbolId id = Add(std::move(symbol));
                m_scopes.Last().Symbols.Add(id);
                Occur(id, m_result.Symbols[id].SelectionRange, true);
                return id;
            }

            Array<SymbolId> Lookup(std::string_view name)
            {
                for (std::size_t index = m_scopes.Size(); index > 0; --index)
                {
                    for (SymbolId local : m_scopes[index - 1].Symbols)
                    {
                        if (m_result.Symbols[local].Name == name)
                        {
                            return {local};
                        }
                    }
                }
                Array<SymbolId> candidates = Members(m_result, m_owner, name);
                if (false == candidates.IsEmpty())
                {
                    return candidates;
                }
                for (const Symbol& symbol : m_result.Symbols)
                {
                    if (InvalidSymbol == symbol.Parent && symbol.Name == name)
                    {
                        candidates.Add(symbol.Id);
                    }
                }
                return candidates;
            }

            String Path(SymbolId id) const
            {
                return String(std::to_string(static_cast<std::uint32_t>(id)));
            }

            void Invalidate(Flow& flow, std::string_view path)
            {
                flow.NonNull.RemoveAll([&](const String& value)
                {
                    return value == path || (value.StartsWith(path) && value.size() > path.size() && '.' == value[path.size()]);
                });
            }

            void InvalidateCalls(Flow& flow, Bool handles, Bool members)
            {
                flow.NonNull.RemoveAll([&](const String& path)
                {
                    std::uint32_t id = 0;
                    const auto parsed = std::from_chars(path.data(), path.data() + path.size(), id);
                    if (std::errc() != parsed.ec || id >= m_result.Symbols.Size())
                    {
                        return true;
                    }
                    const Symbol& symbol = m_result.Symbols[id];
                    return (members && (SymbolKind::Field == symbol.Kind || path.find('.') != String::npos))
                        || (handles && m_handles.Contains(TypeId(m_result, symbol.Type)));
                });
            }

            Bool Convertible(std::string_view destination, const Expression& value) const
            {
                if (value.Type.IsEmpty() || EqualType(destination, value.Type))
                {
                    return true;
                }
                if (value.Type == "Null")
                {
                    return Nullable(destination);
                }
                if (Nullable(destination) && EqualType(Bare(destination), value.Type))
                {
                    return true;
                }
                if (Nullable(value.Type) && EqualType(destination, Bare(value.Type)))
                {
                    return true;
                }
                if (false == TypeArguments(destination).IsEmpty() || false == TypeArguments(value.Type).IsEmpty())
                {
                    return false;
                }
                return Nullable(destination) == Nullable(value.Type)
                    && Derives(m_result, TypeId(m_result, value.Type), TypeId(m_result, destination));
            }

            void CheckAssignable(std::string_view destination, const Expression& value, const SourceRange& range)
            {
                if (false == Convertible(destination, value))
                {
                    Error("JBC3003", "type_mismatch", range, {String(destination), value.Type});
                }
            }

            Expression SymbolExpression(SymbolId id, const SourceRange& range, Flow& flow, Bool write, Bool record, Bool implicit = true)
            {
                const Symbol symbol = m_result.Symbols[id];
                Expression expression;
                expression.Target = id;
                expression.Type = symbol.Type;
                expression.DeclaredType = symbol.Type;
                expression.Path = Path(id);
                expression.LValue = SymbolKind::Field == symbol.Kind || SymbolKind::Local == symbol.Kind || SymbolKind::Parameter == symbol.Kind;
                expression.Writable = symbol.Writable;
                expression.TypeObject = IsTypeSymbol(symbol.Kind);
                if (false == Accessible(m_result, symbol, m_owner))
                {
                    Error("JBC3005", "inaccessible", range, {symbol.Name});
                }
                if (implicit && InvalidSymbol != m_function && m_result.Symbols[m_function].IsStatic
                    && Derives(m_result, m_owner, symbol.Parent) && false == symbol.IsStatic
                    && false == expression.TypeObject && false == IsFunction(symbol.Kind))
                {
                    Error("JBC3028", "static_access", range, {symbol.Name});
                }
                if (false == write && (SymbolKind::Local == symbol.Kind || SymbolKind::Parameter == symbol.Kind)
                    && false == flow.Initialized.Contains(id))
                {
                    Error("JBC3017", "uninitialized", range, {symbol.Name});
                }
                if (flow.NonNull.Contains(expression.Path) && Nullable(expression.Type))
                {
                    expression.Type = Bare(expression.Type);
                }
                if (record)
                {
                    Occur(id, range);
                }
                return expression;
            }

            static Expression Literal(std::string_view type)
            {
                Expression value;
                value.Type = String(type);
                return value;
            }

            Expression Expr(NodeIndex index, Flow& flow, Bool write = false, Bool record = true)
            {
                if (InvalidNode == index)
                {
                    return {};
                }
                const SyntaxNode node = Node(index);
                switch (node.Kind)
                {
                case SyntaxKind::IntegerLiteral:
                    return Literal("Int");
                case SyntaxKind::FloatLiteral:
                    return Literal("Float");
                case SyntaxKind::StringLiteral:
                    return Literal("String");
                case SyntaxKind::BooleanLiteral:
                    return Literal("Bool");
                case SyntaxKind::NullLiteral:
                    return Literal("Null");
                case SyntaxKind::NameExpression:
                case SyntaxKind::GenericNameExpression:
                {
                    if (node.Text == "this")
                    {
                        Expression self = Literal(m_result.Symbols[m_owner].Name);
                        self.Target = m_owner;
                        self.LValue = true;
                        self.Writable = true;
                        return self;
                    }
                    const Array<SymbolId> found = Lookup(node.Text);
                    if (found.IsEmpty())
                    {
                        Error("JBC3004", "unknown_name", NameRange(*m_document, node), {String(node.Text)});
                        return {};
                    }
                    Expression value = SymbolExpression(found[0], NameRange(*m_document, node), flow, write, record);
                    if (IsFunction(m_result.Symbols[found[0]].Kind))
                    {
                        value.Overloads = found;
                    }
                    if (node.Kind == SyntaxKind::GenericNameExpression && value.TypeObject)
                    {
                        ValidateType(index, false, false, true);
                        value.Type = RenderType(m_document->Tree, index);
                        value.DeclaredType = value.Type;
                    }
                    else
                    {
                        for (NodeIndex type : m_document->Tree.GetChildren(index))
                        {
                            ValidateType(type, false, false, true);
                        }
                    }
                    return value;
                }
                case SyntaxKind::ParenthesizedExpression:
                    return Expr(Child(index, 0), flow, write, record);
                case SyntaxKind::NullAssertExpression:
                {
                    Expression value = Expr(Child(index, 0), flow, write, record);
                    value.Type = Bare(value.Type);
                    return value;
                }
                case SyntaxKind::RefArgument:
                {
                    Expression value = Expr(Child(index, 0), flow, false, record);
                    value.Borrowed = true;
                    if (value.DeclaredType.IsNotEmpty())
                    {
                        value.Type = value.DeclaredType;
                    }
                    if (false == value.LValue)
                    {
                        Error("JBC3010", "borrow_argument", node.Range);
                    }
                    return value;
                }
                case SyntaxKind::MemberAccessExpression:
                    return Member(index, flow, write, record);
                case SyntaxKind::CallExpression:
                    return Call(index, flow);
                case SyntaxKind::IndexExpression:
                {
                    Expression object = Expr(Child(index, 0), flow);
                    const Expression key = Expr(Child(index, 1), flow);
                    const Array<String> arguments = TypeArguments(object.Type);
                    const String base = TypeBase(object.Type);
                    if (base == "Array" && arguments.Size() == 1)
                    {
                        CheckAssignable("Int", key, Node(Child(index, 1)).Range);
                        object.Type = arguments[0];
                    }
                    else if (base == "Table" && arguments.Size() == 2)
                    {
                        CheckAssignable(arguments[0], key, Node(Child(index, 1)).Range);
                        object.Type = arguments[1];
                    }
                    else if (base == "String")
                    {
                        CheckAssignable("Int", key, Node(Child(index, 1)).Range);
                        object.Type = "String";
                        object.Writable = false;
                    }
                    else
                    {
                        Error("JBC3029", "not_indexable", node.Range, {object.Type});
                        return {};
                    }
                    object.Path.Clear();
                    object.Target = InvalidSymbol;
                    object.LValue = true;
                    object.DeclaredType = object.Type;
                    if (object.NullSafe && false == Nullable(object.Type))
                    {
                        object.Type = RefType(object.Type);
                    }
                    return object;
                }
                case SyntaxKind::IsNullExpression:
                    Expr(Child(index, 0), flow);
                    return Literal("Bool");
                case SyntaxKind::UnaryExpression:
                {
                    const Expression operand = Expr(Child(index, 0), flow);
                    if (node.Text == "not")
                    {
                        CheckAssignable("Bool", operand, node.Range);
                        return Literal("Bool");
                    }
                    if (operand.Type != "Int" && operand.Type != "Float" && false == operand.Type.IsEmpty())
                    {
                        Error("JBC3031", "operator", node.Range, {String(node.Text), operand.Type});
                    }
                    return Literal(operand.Type);
                }
                case SyntaxKind::BinaryExpression:
                    return Binary(index, flow);
                case SyntaxKind::RangeExpression:
                {
                    const Expression first = Expr(Child(index, 0), flow);
                    const Expression last = Expr(Child(index, 1), flow);
                    CheckAssignable("Int", first, node.Range);
                    CheckAssignable("Int", last, node.Range);
                    return Literal("Range<Int>");
                }
                default:
                    return {};
                }
            }

            String OwnerSubstitution(std::string_view type, std::string_view receiver) const
            {
                return ReceiverSubstitution(type, receiver);
            }

            Expression Member(NodeIndex index, Flow& flow, Bool write, Bool record)
            {
                const SyntaxNode node = Node(index);
                const Expression receiver = Expr(Child(index, 0), flow, false, true);
                const Bool safe = 0 != (node.Flags & SyntaxFlagNullSafe) || receiver.NullSafe;
                const Array<SymbolId> found = Members(m_result, TypeId(m_result, receiver.Type), node.Text);
                if (found.IsEmpty())
                {
                    if (false == receiver.Type.IsEmpty())
                    {
                        Error("JBC3032", "unknown_member", NameRange(*m_document, node), {receiver.Type, String(node.Text)});
                    }
                    return {};
                }
                const Symbol symbol = m_result.Symbols[found[0]];
                Expression value = SymbolExpression(found[0], NameRange(*m_document, node), flow, write, record, false);
                value.Type = OwnerSubstitution(symbol.Type, receiver.Type);
                value.DeclaredType = value.Type;
                value.ReceiverType = receiver.Type;
                value.Path = receiver.Path.IsEmpty() ? String() : String(receiver.Path).Append(".").Append(node.Text);
                value.Writable = symbol.Writable && (receiver.Writable || false == receiver.LValue);
                value.NullSafe = safe;
                value.StaticReceiver = receiver.TypeObject;
                if (IsFunction(symbol.Kind))
                {
                    value.Overloads = found;
                }
                if (receiver.TypeObject && false == symbol.IsStatic && SymbolKind::EnumMember != symbol.Kind && false == IsFunction(symbol.Kind))
                {
                    Error("JBC3028", "static_access", NameRange(*m_document, node), {symbol.Name});
                }
                for (std::size_t slot = 1; slot < node.ChildCount; ++slot)
                {
                    ValidateType(Child(index, slot), false, false, true);
                }
                if (safe && value.Type != "Void" && false == Nullable(value.Type))
                {
                    value.Type = RefType(value.Type);
                }
                else if (flow.NonNull.Contains(value.Path) && Nullable(value.Type))
                {
                    value.Type = Bare(value.Type);
                }
                return value;
            }

            Expression Binary(NodeIndex index, Flow& flow)
            {
                const SyntaxNode node = Node(index);
                Expression left = Expr(Child(index, 0), flow);
                Flow rightFlow = flow;
                if (node.Text == "and" || node.Text == "or")
                {
                    Narrow(Child(index, 0), node.Text == "and", rightFlow);
                }
                Expression right = Expr(Child(index, 1), rightFlow);
                if (node.Text == "and" || node.Text == "or")
                {
                    CheckAssignable("Bool", left, Node(Child(index, 0)).Range);
                    CheckAssignable("Bool", right, Node(Child(index, 1)).Range);
                    Intersect(flow, rightFlow);
                    return Literal("Bool");
                }
                flow = std::move(rightFlow);
                if (node.Text == "??")
                {
                    if (false == Nullable(left.Type) && left.Type != "Null" && false == left.Type.IsEmpty())
                    {
                        Error("JBC3030", "not_nullable", Node(Child(index, 0)).Range, {left.Type});
                    }
                    if (left.Type == "Null")
                    {
                        return Literal(right.Type);
                    }
                    const String bare = Bare(left.Type);
                    CheckAssignable(bare, right, Node(Child(index, 1)).Range);
                    return Literal(bare);
                }
                if (node.Text == "==" || node.Text == "!=")
                {
                    if (false == Convertible(left.Type, right) && false == Convertible(right.Type, left))
                    {
                        Error("JBC3003", "type_mismatch", node.Range, {left.Type, right.Type});
                    }
                    return Literal("Bool");
                }
                const Bool comparison = node.Text == "<" || node.Text == "<=" || node.Text == ">" || node.Text == ">=";
                const Bool vectorScale = (left.Type == "Vector2" && right.Type == "Float" && (node.Text == "*" || node.Text == "/"))
                    || (left.Type == "Float" && right.Type == "Vector2" && node.Text == "*");
                if (vectorScale)
                {
                    return Literal("Vector2");
                }
                if (false == left.Type.IsEmpty() && false == right.Type.IsEmpty())
                {
                    if (left.Type != right.Type)
                    {
                        Error("JBC3003", "type_mismatch", node.Range, {left.Type, right.Type});
                    }
                    const Bool numeric = left.Type == "Int" || left.Type == "Float";
                    const Bool stringOperation = left.Type == "String" && (node.Text == "+" || comparison);
                    const Bool vectorOperation = TypeBase(left.Type) == "Vector2" && (node.Text == "+" || node.Text == "-");
                    if (false == numeric && false == stringOperation && false == vectorOperation)
                    {
                        Error("JBC3031", "operator", node.Range, {String(node.Text), left.Type});
                    }
                    if (node.Text == "%" && left.Type != "Int")
                    {
                        Error("JBC3031", "operator", node.Range, {String(node.Text), left.Type});
                    }
                }
                return Literal(comparison ? String("Bool") : left.Type);
            }

            void Narrow(NodeIndex index, Bool truth, Flow& flow)
            {
                if (InvalidNode == index)
                {
                    return;
                }
                const SyntaxNode node = Node(index);
                if (SyntaxKind::ParenthesizedExpression == node.Kind)
                {
                    Narrow(Child(index, 0), truth, flow);
                }
                else if (SyntaxKind::UnaryExpression == node.Kind && node.Text == "not")
                {
                    Narrow(Child(index, 0), false == truth, flow);
                }
                else if (SyntaxKind::IsNullExpression == node.Kind)
                {
                    const Expression value = Expr(Child(index, 0), flow, true, false);
                    const Bool nonNull = truth == (0 != (node.Flags & SyntaxFlagNegated));
                    if (value.Path.IsNotEmpty())
                    {
                        Invalidate(flow, value.Path);
                        if (nonNull)
                        {
                            flow.NonNull.Add(value.Path);
                        }
                    }
                }
                else if (SyntaxKind::BinaryExpression == node.Kind && ((node.Text == "and" && truth) || (node.Text == "or" && false == truth)))
                {
                    Narrow(Child(index, 0), truth, flow);
                    Narrow(Child(index, 1), truth, flow);
                }
                else if (SyntaxKind::CallExpression == node.Kind)
                {
                    for (const CallSite& call : m_document->Calls)
                    {
                        if (call.Range.Begin.Offset == node.Range.Begin.Offset && call.Range.End.Offset == node.Range.End.Offset
                            && InvalidSymbol != call.Resolved)
                        {
                            const Symbol& symbol = m_result.Symbols[call.Resolved];
                            InvalidateCalls(flow, symbol.MayInvalidateHandles, false == symbol.Api || symbol.MutatesReceiver);
                        }
                    }
                }
            }

            static void Intersect(Flow& left, const Flow& right)
            {
                left.Initialized.RemoveAll([&](SymbolId id) { return false == right.Initialized.Contains(id); });
                left.NonNull.RemoveAll([&](const String& path) { return false == right.NonNull.Contains(path); });
            }

            static void MergeExits(ControlFlow& target, const ControlFlow& source, Bool consumeBreak = false)
            {
                target.Returns |= source.Returns;
                if (source.Breaks && false == consumeBreak)
                {
                    if (target.Breaks)
                    {
                        Intersect(target.BreakFlow, source.BreakFlow);
                    }
                    else
                    {
                        target.BreakFlow = source.BreakFlow;
                    }
                    target.Breaks = true;
                }
                if (source.Continues)
                {
                    if (target.Continues)
                    {
                        Intersect(target.ContinueFlow, source.ContinueFlow);
                    }
                    else
                    {
                        target.ContinueFlow = source.ContinueFlow;
                    }
                    target.Continues = true;
                }
            }

            Expression Call(NodeIndex index, Flow& flow)
            {
                const SyntaxNode node = Node(index);
                const NodeIndex calleeNode = Child(index, 0);
                if (InvalidNode == calleeNode)
                {
                    return {};
                }
                const SyntaxNode calleeSyntax = Node(calleeNode);
                Expression callee = Expr(calleeNode, flow, false, false);
                Array<Expression> arguments;
                CallSite call;
                call.Range = node.Range;
                call.CalleeRange = NameRange(*m_document, calleeSyntax);
                for (std::size_t slot = 1; slot < node.ChildCount; ++slot)
                {
                    const NodeIndex argument = Child(index, slot);
                    arguments.Add(Expr(argument, flow));
                    call.Arguments.Add(Node(argument).Range);
                }
                if (callee.TypeObject)
                {
                    const SymbolId type = callee.Target;
                    if (m_handles.Contains(type) || Abstract(type) || SymbolKind::Interface == m_result.Symbols[type].Kind)
                    {
                        Error("JBC3016", "abstract_value", node.Range, {callee.Type});
                    }
                    if ((callee.Type == "Int" || callee.Type == "Float") && arguments.Size() == 1
                        && (arguments[0].Type == "Int" || arguments[0].Type == "Float"))
                    {
                        call.Resolved = type;
                        call.Overloads.Add(type);
                        Occur(type, call.CalleeRange);
                        m_document->Calls.Add(std::move(call));
                        return Literal(callee.Type);
                    }
                    callee.Overloads = Members(m_result, type, m_result.Symbols[type].Name);
                    callee.Overloads.RemoveAll([&](SymbolId id) { return SymbolKind::Constructor != m_result.Symbols[id].Kind; });
                    if (callee.Overloads.IsEmpty() && arguments.IsEmpty())
                    {
                        call.Resolved = type;
                        call.Overloads.Add(type);
                        Occur(type, call.CalleeRange);
                        m_document->Calls.Add(std::move(call));
                        return Literal(callee.Type);
                    }
                }
                if (callee.Overloads.IsEmpty())
                {
                    if (false == callee.Type.IsEmpty())
                    {
                        Error("JBC3033", "not_callable", call.CalleeRange, {callee.Type});
                    }
                    m_document->Calls.Add(std::move(call));
                    return {};
                }
                call.Overloads = callee.Overloads;
                Array<String> explicitTypes;
                const std::size_t genericStart = SyntaxKind::MemberAccessExpression == calleeSyntax.Kind ? 1 : 0;
                for (std::size_t slot = genericStart; slot < calleeSyntax.ChildCount; ++slot)
                {
                    explicitTypes.Add(RenderType(m_document->Tree, Child(calleeNode, slot)));
                }
                if (explicitTypes.Size() == 1 && InvalidSymbol != callee.Target && m_result.Symbols[callee.Target].Api)
                {
                    const String name = m_result.Symbols[callee.Target].Name;
                    const SymbolId argument = TypeId(m_result, explicitTypes[0]);
                    Bool valid = true;
                    if (name == "GetComponent" || name == "AddComponent")
                    {
                        valid = m_handles.Contains(argument) && false == m_scripts.Contains(argument)
                            && TypeBase(explicitTypes[0]) != "GameObject" && TypeBase(explicitTypes[0]) != "Layer"
                            && false == Nullable(explicitTypes[0]);
                    }
                    else if (name == "GetScript" || name == "AddScript")
                    {
                        valid = m_scripts.Contains(argument) && false == Nullable(explicitTypes[0]);
                    }
                    else if (name == "Send" || name == "Broadcast")
                    {
                        Array<SymbolId> visiting;
                        valid = PodValue(explicitTypes[0], visiting);
                    }
                    if (false == valid)
                    {
                        Error("JBC3039", "type_constraint", call.CalleeRange, {name, explicitTypes[0]});
                    }
                }
                Int bestScore = std::numeric_limits<std::int64_t>::max();
                SymbolId best = InvalidSymbol;
                Bool ambiguous = false;
                Bool borrowMismatch = false;
                Bool requiresInstance = false;
                for (SymbolId candidate : callee.Overloads)
                {
                    const Symbol& symbol = m_result.Symbols[candidate];
                    if (false == Accessible(m_result, symbol, m_owner) || arguments.Size() > symbol.Parameters.Size()
                        || explicitTypes.Size() != symbol.TypeParameters.Size())
                    {
                        continue;
                    }
                    const Bool implicitCall = SyntaxKind::NameExpression == calleeSyntax.Kind || SyntaxKind::GenericNameExpression == calleeSyntax.Kind;
                    const Bool staticContext = callee.StaticReceiver || (implicitCall && InvalidSymbol != m_function
                        && m_result.Symbols[m_function].IsStatic && Derives(m_result, m_owner, symbol.Parent));
                    if (staticContext && false == symbol.IsStatic)
                    {
                        requiresInstance = true;
                        continue;
                    }
                    Int score = 0;
                    Bool accepts = true;
                    for (std::size_t slot = 0; slot < symbol.Parameters.Size(); ++slot)
                    {
                        if (slot >= arguments.Size())
                        {
                            accepts &= symbol.Parameters[slot].HasDefault;
                            score += 2;
                            continue;
                        }
                        const ParameterInfo& parameter = symbol.Parameters[slot];
                        String expected = OwnerSubstitution(parameter.Type, callee.ReceiverType);
                        expected = Substitute(expected, symbol.TypeParameters, explicitTypes);
                        const Expression& argument = arguments[slot];
                        if (parameter.Borrowed != argument.Borrowed)
                        {
                            borrowMismatch = true;
                            accepts = false;
                        }
                        if (parameter.Borrowed && (false == argument.LValue || (false == argument.Writable && false == parameter.Type.StartsWith("const "))))
                        {
                            accepts = false;
                        }
                        if (expected.StartsWith("const "))
                        {
                            expected = expected.Substr(6);
                        }
                        if (false == Convertible(expected, argument))
                        {
                            accepts = false;
                        }
                        if (false == EqualType(expected, argument.Type))
                        {
                            score += 1;
                        }
                    }
                    if (accepts && score < bestScore)
                    {
                        bestScore = score;
                        best = candidate;
                        ambiguous = false;
                    }
                    else if (accepts && score == bestScore)
                    {
                        ambiguous = true;
                    }
                }
                if (InvalidSymbol == best)
                {
                    if (requiresInstance)
                    {
                        Error("JBC3028", "static_access", call.CalleeRange, {String(calleeSyntax.Text)});
                    }
                    else if (borrowMismatch)
                    {
                        Error("JBC3010", "borrow_argument", call.CalleeRange);
                    }
                    else
                    {
                        Error("JBC3034", "no_overload", call.CalleeRange, {String(calleeSyntax.Text)});
                    }
                }
                else if (ambiguous)
                {
                    Error("JBC3035", "ambiguous_overload", call.CalleeRange, {String(calleeSyntax.Text)});
                    best = InvalidSymbol;
                }
                Expression result;
                if (InvalidSymbol != best)
                {
                    const Symbol symbol = m_result.Symbols[best];
                    call.Resolved = best;
                    Occur(best, call.CalleeRange);
                    result.Type = OwnerSubstitution(symbol.Type, callee.ReceiverType);
                    result.Type = Substitute(result.Type, symbol.TypeParameters, explicitTypes);
                    result.NullSafe = callee.NullSafe;
                    if (callee.NullSafe && result.Type != "Void" && false == Nullable(result.Type))
                    {
                        result.Type = RefType(result.Type);
                    }
                    if (symbol.MutatesReceiver && callee.Path.IsNotEmpty())
                    {
                        const std::size_t lastDot = callee.Path.rfind('.');
                        const String receiverPath = String::npos == lastDot ? callee.Path : callee.Path.Substr(0, lastDot);
                        Invalidate(flow, receiverPath);
                        for (const String& iterated : m_iterated)
                        {
                            const String container = TypeBase(callee.ReceiverType);
                            const Bool changesSize = (container == "Array" || container == "Table")
                                && symbol.Name != "Reserve" && symbol.Name != "Shrink";
                            if (iterated == receiverPath && changesSize)
                            {
                                Error("JBC3019", "iteration_mutation", node.Range);
                            }
                        }
                    }
                    InvalidateCalls(flow, symbol.MayInvalidateHandles, false == symbol.Api || symbol.MutatesReceiver);
                    for (const Expression& argument : arguments)
                    {
                        if (argument.Borrowed && argument.Path.IsNotEmpty())
                        {
                            Invalidate(flow, argument.Path);
                        }
                    }
                }
                m_document->Calls.Add(std::move(call));
                return result;
            }

            Bool PodValue(std::string_view type, Array<SymbolId>& visiting) const
            {
                const String base = TypeBase(type);
                if (Nullable(type) || base == "String" || base == "Array" || base == "Table" || base == "Void")
                {
                    return false;
                }
                const SymbolId id = TypeId(m_result, base);
                if (InvalidSymbol == id || visiting.Contains(id))
                {
                    return false;
                }
                const Symbol& symbol = m_result.Symbols[id];
                if (symbol.Api)
                {
                    return SymbolKind::Enum == symbol.Kind || (SymbolKind::Type == symbol.Kind && false == m_handles.Contains(id));
                }
                if (SymbolKind::Enum == symbol.Kind)
                {
                    return true;
                }
                if (SymbolKind::Struct != symbol.Kind)
                {
                    return false;
                }
                visiting.Add(id);
                for (SymbolId member : Members(m_result, id))
                {
                    if (SymbolKind::Field == m_result.Symbols[member].Kind && false == PodValue(m_result.Symbols[member].Type, visiting))
                    {
                        visiting.Pop();
                        return false;
                    }
                }
                visiting.Pop();
                return true;
            }

            ControlFlow Block(NodeIndex block, Flow& flow, Bool newScope = true)
            {
                if (InvalidNode == block)
                {
                    return {};
                }
                if (newScope)
                {
                    m_scopes.Add({Node(block).Range, {}});
                }
                ControlFlow completion;
                Flow unreachable;
                Bool haveUnreachableFlow = false;
                for (NodeIndex statement : m_document->Tree.GetChildren(block))
                {
                    if (completion.FallsThrough)
                    {
                        const ControlFlow next = Statement(statement, flow);
                        completion.FallsThrough = next.FallsThrough;
                        MergeExits(completion, next);
                    }
                    else
                    {
                        if (false == haveUnreachableFlow)
                        {
                            unreachable = flow;
                            haveUnreachableFlow = true;
                        }
                        Statement(statement, unreachable);
                    }
                }
                if (newScope)
                {
                    m_scopes.Pop();
                }
                return completion;
            }

            ControlFlow LoopBody(NodeIndex body, Flow& flow, Bool newScope, NodeIndex condition = InvalidNode)
            {
                const Flow initial = flow;
                Flow entry = flow;
                const std::size_t symbolCount = m_result.Symbols.Size();
                const std::size_t diagnosticCount = m_document->Diagnostics.Size();
                const std::size_t occurrenceCount = m_document->Occurrences.Size();
                const std::size_t callCount = m_document->Calls.Size();
                const Array<SymbolId> scopeSymbols = false == newScope ? m_scopes.Last().Symbols : Array<SymbolId>();
                while (true)
                {
                    Flow falseExit = entry;
                    flow = entry;
                    if (InvalidNode != condition)
                    {
                        const Expression value = Expr(condition, falseExit);
                        CheckAssignable("Bool", value, Node(condition).Range);
                        flow = falseExit;
                        const std::size_t checkedDiagnostics = m_document->Diagnostics.Size();
                        const std::size_t checkedOccurrences = m_document->Occurrences.Size();
                        const std::size_t checkedCalls = m_document->Calls.Size();
                        Narrow(condition, true, flow);
                        Narrow(condition, false, falseExit);
                        // Expr checks the condition; Narrow only extracts facts from it.
                        m_document->Diagnostics.Resize(checkedDiagnostics);
                        m_document->Occurrences.Resize(checkedOccurrences);
                        m_document->Calls.Resize(checkedCalls);
                    }
                    const ControlFlow completion = Block(body, flow, newScope);
                    if (false == completion.FallsThrough && false == completion.Continues)
                    {
                        if (InvalidNode != condition)
                        {
                            if (completion.Breaks)
                            {
                                Intersect(falseExit, completion.BreakFlow);
                            }
                            flow = std::move(falseExit);
                        }
                        return completion;
                    }
                    Flow backEdge = completion.FallsThrough ? flow : completion.ContinueFlow;
                    if (completion.FallsThrough && completion.Continues)
                    {
                        Intersect(backEdge, completion.ContinueFlow);
                    }
                    Flow repeat = initial;
                    Intersect(repeat, backEdge);
                    const Bool sameInitialized = entry.Initialized.Size() == repeat.Initialized.Size()
                        && nullptr == entry.Initialized.FindBy([&](SymbolId id) { return false == repeat.Initialized.Contains(id); });
                    const Bool sameNonNull = entry.NonNull.Size() == repeat.NonNull.Size()
                        && nullptr == entry.NonNull.FindBy([&](const String& path) { return false == repeat.NonNull.Contains(path); });
                    if (sameInitialized && sameNonNull)
                    {
                        if (InvalidNode != condition)
                        {
                            if (completion.Breaks)
                            {
                                Intersect(falseExit, completion.BreakFlow);
                            }
                            flow = std::move(falseExit);
                        }
                        return completion;
                    }
                    m_result.Symbols.Resize(symbolCount);
                    m_document->Diagnostics.Resize(diagnosticCount);
                    m_document->Occurrences.Resize(occurrenceCount);
                    m_document->Calls.Resize(callCount);
                    if (false == newScope)
                    {
                        m_scopes.Last().Symbols = scopeSymbols;
                    }
                    entry = std::move(repeat);
                    flow = entry;
                }
            }

            ControlFlow Statement(NodeIndex index, Flow& flow)
            {
                if (InvalidNode == index)
                {
                    return {};
                }
                const SyntaxNode node = Node(index);
                switch (node.Kind)
                {
                case SyntaxKind::Block:
                    return Block(index, flow);
                case SyntaxKind::LocalDeclaration:
                {
                    const NodeIndex typeNode = Child(index, 0);
                    ValidateType(typeNode, false);
                    Symbol symbol = FromNode(index, m_function, SymbolKind::Local);
                    symbol.Type = RenderType(m_document->Tree, typeNode);
                    symbol.ScopeRange = m_scopes.Last().Range;
                    symbol.Writable = InvalidNode == typeNode || 0 == (Node(typeNode).Flags & SyntaxFlagConst);
                    const NodeIndex initializer = Child(index, 1);
                    Expression initial;
                    if (InvalidNode != initializer)
                    {
                        initial = Expr(initializer, flow);
                        CheckAssignable(symbol.Type, initial, Node(initializer).Range);
                    }
                    const SymbolId id = AddLocal(std::move(symbol));
                    const SymbolId localType = TypeId(m_result, m_result.Symbols[id].Type);
                    const Bool defaultInitialized = InvalidSymbol != localType && m_result.Symbols[id].Type != "Int"
                        && m_result.Symbols[id].Type != "Float" && m_result.Symbols[id].Type != "Bool"
                        && (Nullable(m_result.Symbols[id].Type) || SymbolKind::Enum != m_result.Symbols[localType].Kind);
                    if (InvalidNode != initializer || defaultInitialized)
                    {
                        flow.Initialized.Add(id);
                        if (Nullable(m_result.Symbols[id].Type) && false == Nullable(initial.Type) && initial.Type != "Null"
                            && false == initial.Type.IsEmpty())
                        {
                            flow.NonNull.Add(Path(id));
                        }
                    }
                    return {};
                }
                case SyntaxKind::ExpressionStatement:
                    Expr(Child(index, 0), flow);
                    return {};
                case SyntaxKind::AssignmentStatement:
                {
                    const Bool compound = node.Text != "=";
                    Expression target = Expr(Child(index, 0), flow, false == compound);
                    const Expression value = Expr(Child(index, 1), flow);
                    if (false == target.LValue || false == target.Writable)
                    {
                        Error("JBC3006", "readonly", Node(Child(index, 0)).Range);
                    }
                    const String destination = target.DeclaredType.IsNotEmpty() ? target.DeclaredType : target.Type;
                    const Bool scaleAssignment = compound && target.Type == "Vector2" && value.Type == "Float"
                        && (node.Text == "*=" || node.Text == "/=");
                    if (false == scaleAssignment)
                    {
                        CheckAssignable(destination, value, Node(Child(index, 1)).Range);
                    }
                    if (compound && false == target.Type.IsEmpty())
                    {
                        const Bool numeric = target.Type == "Int" || target.Type == "Float";
                        const Bool stringAdd = target.Type == "String" && node.Text == "+=";
                        const Bool vectorAdd = target.Type == "Vector2" && (node.Text == "+=" || node.Text == "-=");
                        const Bool vectorScale = target.Type == "Vector2" && value.Type == "Float" && (node.Text == "*=" || node.Text == "/=");
                        if (false == numeric && false == stringAdd && false == vectorAdd && false == vectorScale)
                        {
                            Error("JBC3031", "operator", node.Range, {String(node.Text), target.Type});
                        }
                    }
                    if (InvalidSymbol != target.Target && false == flow.Initialized.Contains(target.Target))
                    {
                        flow.Initialized.Add(target.Target);
                    }
                    if (target.Path.IsNotEmpty())
                    {
                        Invalidate(flow, target.Path);
                        if (Nullable(destination) && false == Nullable(value.Type) && value.Type != "Null" && false == value.Type.IsEmpty())
                        {
                            flow.NonNull.Add(target.Path);
                        }
                    }
                    return {};
                }
                case SyntaxKind::ReturnStatement:
                {
                    const Expression value = Expr(Child(index, 0), flow);
                    const String expected = SymbolKind::Constructor == m_result.Symbols[m_function].Kind ? String("Void") : m_result.Symbols[m_function].Type;
                    if (InvalidNode == Child(index, 0))
                    {
                        if (expected != "Void")
                        {
                            Error("JBC3003", "type_mismatch", node.Range, {expected, "Void"});
                        }
                    }
                    else
                    {
                        CheckAssignable(expected, value, node.Range);
                    }
                    return {false, true, false, false};
                }
                case SyntaxKind::IfStatement:
                {
                    const Expression condition = Expr(Child(index, 0), flow);
                    CheckAssignable("Bool", condition, Node(Child(index, 0)).Range);
                    Flow yes = flow;
                    Flow no = flow;
                    Narrow(Child(index, 0), true, yes);
                    Narrow(Child(index, 0), false, no);
                    const ControlFlow yesCompletion = Block(Child(index, 1), yes);
                    const NodeIndex otherwise = Child(index, 2);
                    const ControlFlow noCompletion = InvalidNode != otherwise ? Statement(otherwise, no) : ControlFlow();
                    if (false == yesCompletion.FallsThrough && noCompletion.FallsThrough)
                    {
                        flow = std::move(no);
                    }
                    else if (false == noCompletion.FallsThrough && yesCompletion.FallsThrough)
                    {
                        flow = std::move(yes);
                    }
                    else
                    {
                        Intersect(yes, no);
                        flow = std::move(yes);
                    }
                    ControlFlow completion;
                    completion.FallsThrough = yesCompletion.FallsThrough || noCompletion.FallsThrough;
                    MergeExits(completion, yesCompletion);
                    MergeExits(completion, noCompletion);
                    return completion;
                }
                case SyntaxKind::WhileStatement:
                {
                    ++m_loopDepth;
                    const ControlFlow completion = LoopBody(Child(index, 1), flow, true, Child(index, 0));
                    --m_loopDepth;
                    return {true, completion.Returns, false, false};
                }
                case SyntaxKind::ForStatement:
                {
                    const Expression iterable = Expr(Child(index, 2), flow);
                    const String base = TypeBase(iterable.Type);
                    const Array<String> arguments = TypeArguments(iterable.Type);
                    String firstType;
                    String secondType;
                    if (base == "Range" || base == "Array")
                    {
                        firstType = arguments.Size() == 1 ? arguments[0] : String();
                    }
                    else if (base == "Table" && arguments.Size() == 2)
                    {
                        firstType = arguments[0];
                        secondType = arguments[1];
                    }
                    else if (false == iterable.Type.IsEmpty())
                    {
                        Error("JBC3036", "not_iterable", Node(Child(index, 2)).Range, {iterable.Type});
                    }
                    if ((InvalidNode != Child(index, 1)) != secondType.IsNotEmpty())
                    {
                        Error("JBC3037", "for_variables", node.Range);
                    }
                    const NodeIndex body = Child(index, 3);
                    if (InvalidNode == body)
                    {
                        return {};
                    }
                    m_scopes.Add({Node(body).Range, {}});
                    Flow loop = flow;
                    for (std::size_t slot = 0; slot < 2; ++slot)
                    {
                        const NodeIndex variable = Child(index, slot);
                        if (InvalidNode == variable)
                        {
                            continue;
                        }
                        Symbol symbol = FromNode(variable, m_function, SymbolKind::Local);
                        symbol.Type = 0 == slot ? firstType : secondType;
                        symbol.ScopeRange = Node(body).Range;
                        const Bool borrowed = 0 != (Node(variable).Flags & SyntaxFlagRef);
                        symbol.Writable = false == borrowed || iterable.Writable || false == iterable.LValue;
                        if (base == "Table" && 0 == slot && borrowed)
                        {
                            symbol.Writable = false;
                        }
                        if (base == "Range" && 0 != (Node(variable).Flags & SyntaxFlagRef))
                        {
                            Error("JBC3010", "borrow_argument", Node(variable).Range);
                        }
                        loop.Initialized.Add(AddLocal(std::move(symbol)));
                    }
                    ++m_loopDepth;
                    m_iterated.Add(iterable.Path);
                    const ControlFlow completion = LoopBody(body, loop, false);
                    m_iterated.Pop();
                    --m_loopDepth;
                    m_scopes.Pop();
                    if (completion.FallsThrough)
                    {
                        Intersect(flow, loop);
                    }
                    if (completion.Continues)
                    {
                        Intersect(flow, completion.ContinueFlow);
                    }
                    if (completion.Breaks)
                    {
                        Intersect(flow, completion.BreakFlow);
                    }
                    return {true, completion.Returns, false, false};
                }
                case SyntaxKind::SwitchStatement:
                    return Switch(index, flow);
                case SyntaxKind::BreakStatement:
                {
                    if (0 == m_loopDepth && 0 == m_switchDepth)
                    {
                        Error("JBC3038", "jump_context", node.Range, {"break"});
                    }
                    ControlFlow completion{false, false, true, false};
                    completion.BreakFlow = flow;
                    return completion;
                }
                case SyntaxKind::ContinueStatement:
                {
                    if (0 == m_loopDepth)
                    {
                        Error("JBC3038", "jump_context", node.Range, {"continue"});
                    }
                    ControlFlow completion{false, false, false, true};
                    completion.ContinueFlow = flow;
                    return completion;
                }
                default:
                    return {};
                }
            }

            ControlFlow Switch(NodeIndex index, Flow& flow)
            {
                const Expression value = Expr(Child(index, 0), flow);
                const SymbolId type = TypeId(m_result, value.Type);
                Array<SymbolId> covered;
                Bool defaultPresent = false;
                ControlFlow completion{false, false, false, false};
                Bool first = true;
                Flow merged;
                ++m_switchDepth;
                for (std::size_t slot = 1; slot < Node(index).ChildCount; ++slot)
                {
                    const NodeIndex clause = Child(index, slot);
                    const SyntaxNode clauseNode = Node(clause);
                    const Bool isDefault = SyntaxKind::DefaultClause == clauseNode.Kind;
                    if (isDefault)
                    {
                        if (defaultPresent)
                        {
                            Error("JBC3002", "duplicate", clauseNode.Range, {"default"});
                        }
                        defaultPresent = true;
                    }
                    else
                    {
                        for (std::size_t caseSlot = 0; caseSlot + 1 < clauseNode.ChildCount; ++caseSlot)
                        {
                            const NodeIndex caseValue = Child(clause, caseSlot);
                            const Expression caseExpression = Expr(caseValue, flow);
                            CheckAssignable(value.Type, caseExpression, Node(caseValue).Range);
                            if (InvalidSymbol != caseExpression.Target)
                            {
                                if (covered.Contains(caseExpression.Target))
                                {
                                    Error("JBC3002", "duplicate", Node(caseValue).Range, {m_result.Symbols[caseExpression.Target].Name});
                                }
                                covered.Add(caseExpression.Target);
                            }
                        }
                    }
                    Flow branch = flow;
                    const ControlFlow branchCompletion = Block(Child(clause, clauseNode.ChildCount - 1), branch);
                    completion.FallsThrough |= branchCompletion.FallsThrough || branchCompletion.Breaks;
                    MergeExits(completion, branchCompletion, true);
                    if (false == branchCompletion.FallsThrough && false == branchCompletion.Breaks)
                    {
                        continue;
                    }
                    if (branchCompletion.Breaks)
                    {
                        if (branchCompletion.FallsThrough)
                        {
                            Intersect(branch, branchCompletion.BreakFlow);
                        }
                        else
                        {
                            branch = branchCompletion.BreakFlow;
                        }
                    }
                    if (first)
                    {
                        merged = branch;
                        first = false;
                    }
                    else
                    {
                        Intersect(merged, branch);
                    }
                }
                --m_switchDepth;
                Bool exhaustive = defaultPresent;
                if (InvalidSymbol != type && SymbolKind::Enum == m_result.Symbols[type].Kind)
                {
                    exhaustive = true;
                    for (SymbolId member : Members(m_result, type))
                    {
                        if (SymbolKind::EnumMember == m_result.Symbols[member].Kind && false == covered.Contains(member) && false == defaultPresent)
                        {
                            exhaustive = false;
                        }
                    }
                    if (false == exhaustive)
                    {
                        Error("JBC3018", "switch_exhaustive", Node(index).Range, {value.Type});
                    }
                }
                if (false == first)
                {
                    if (false == exhaustive)
                    {
                        Intersect(merged, flow);
                    }
                    flow = std::move(merged);
                }
                completion.FallsThrough |= false == exhaustive || Node(index).ChildCount <= 1;
                return completion;
            }

            AnalysisResult& m_result;
            const ApiCatalog& m_api;
            const AnalysisResult* m_previous = nullptr;
            Array<String> m_reused;
            Array<ReusedBody> m_reusedBodies;
            Array<Binding> m_bindings;
            Array<SymbolId> m_handles;
            Array<SymbolId> m_scripts;
            Array<Scope> m_scopes;
            Array<String> m_iterated;
            DocumentAnalysis* m_document = nullptr;
            SymbolId m_owner = InvalidSymbol;
            SymbolId m_function = InvalidSymbol;
            UInt32 m_loopDepth = 0;
            UInt32 m_switchDepth = 0;
        };
    }

    OwnerPtr<AnalysisResult> AnalyzeProject(ArrayView<const DocumentInput> documents, const ApiCatalog& api)
    {
        auto result = MakeOwnerPtr<AnalysisResult>();
        for (const DocumentInput& input : documents)
        {
            auto document = MakeOwnerPtr<DocumentAnalysis>(input);
            document->Tree = Parse(document->Source, document->ParseDiagnostics);
            result->Documents.Add(std::move(document));
        }
        result->ParsedDocuments = documents.Size();
        result->CheckedDocuments = documents.Size();
        Analyzer analyzer(*result, api);
        analyzer.Run();
        return result;
    }

    OwnerPtr<AnalysisResult> AnalyzeProjectIncremental(ArrayView<const DocumentInput> documents,
        const ApiCatalog& api, OwnerPtr<AnalysisResult> previous)
    {
        if (nullptr == previous.Get())
        {
            return AnalyzeProject(documents, api);
        }
        auto result = MakeOwnerPtr<AnalysisResult>();
        Array<String> affected;
        Array<String> changedNames;
        const auto addNames = [&](const DocumentAnalysis& document)
        {
            for (NodeIndex index = 0; index < document.Tree.GetNodeCount(); ++index)
            {
                const SyntaxNode& node = document.Tree.Get(index);
                if (node.Kind == SyntaxKind::ScriptDeclaration || node.Kind == SyntaxKind::ClassDeclaration
                    || node.Kind == SyntaxKind::StructDeclaration || node.Kind == SyntaxKind::InterfaceDeclaration
                    || node.Kind == SyntaxKind::EnumDeclaration || node.Kind == SyntaxKind::FunctionDeclaration
                    || node.Kind == SyntaxKind::FieldDeclaration || node.Kind == SyntaxKind::EnumMember)
                {
                    if (false == node.Text.empty() && false == changedNames.Contains(String(node.Text)))
                    {
                        changedNames.Add(String(node.Text));
                    }
                }
            }
        };
        for (const auto& old : previous->Documents)
        {
            const DocumentInput* current = nullptr;
            for (const DocumentInput& input : documents)
            {
                if (input.Uri == old->Input.Uri)
                {
                    current = &input;
                    break;
                }
            }
            if (nullptr == current || current->Text != old->Input.Text || current->Path != old->Input.Path)
            {
                affected.Add(old->Input.Uri);
                if (nullptr == current)
                {
                    addNames(*old);
                }
            }
        }
        for (const DocumentInput& input : documents)
        {
            OwnerPtr<DocumentAnalysis>* old = previous->Documents.FindBy([&](const auto& document)
            {
                return nullptr != document.Get() && document->Input.Uri == input.Uri
                    && document->Input.Text == input.Text && document->Input.Path == input.Path;
            });
            if (nullptr != old)
            {
                (*old)->Input = input;
                result->Documents.Add(std::move(*old));
            }
            else
            {
                auto document = MakeOwnerPtr<DocumentAnalysis>(input);
                document->Tree = Parse(document->Source, document->ParseDiagnostics);
                const DocumentAnalysis* oldDocument = FindDocument(*previous, input.Uri);
                if (nullptr == oldDocument || DeclarationShape(*oldDocument) != DeclarationShape(*document))
                {
                    if (nullptr != oldDocument)
                    {
                        addNames(*oldDocument);
                    }
                    addNames(*document);
                }
                result->Documents.Add(std::move(document));
                if (false == affected.Contains(input.Uri))
                {
                    affected.Add(input.Uri);
                }
                ++result->ParsedDocuments;
            }
        }
        for (const auto& document : result->Documents)
        {
            if (affected.Contains(document->Input.Uri))
            {
                continue;
            }
            DiagnosticList ignored;
            const Array<Token> tokens = Lex(document->Source, ignored);
            for (const Token& token : tokens)
            {
                if (TokenKind::Identifier == token.Kind && changedNames.Contains(String(token.Text)))
                {
                    affected.Add(document->Input.Uri);
                    break;
                }
            }
        }
        Bool propagated = true;
        while (propagated)
        {
            propagated = false;
            for (const auto& document : result->Documents)
            {
                if (affected.Contains(document->Input.Uri))
                {
                    continue;
                }
                for (const Occurrence& occurrence : document->Occurrences)
                {
                    if (InvalidSymbol != occurrence.Target && occurrence.Target < previous->Symbols.Size()
                        && affected.Contains(previous->Symbols[occurrence.Target].Uri))
                    {
                        affected.Add(document->Input.Uri);
                        propagated = true;
                        break;
                    }
                }
            }
        }
        Array<String> reused;
        for (const auto& document : result->Documents)
        {
            if (false == affected.Contains(document->Input.Uri))
            {
                reused.Add(document->Input.Uri);
            }
            else
            {
                ++result->CheckedDocuments;
            }
        }
        Analyzer analyzer(*result, api, previous.Get(), std::move(reused));
        analyzer.Run();
        return result;
    }

    const Symbol* FindSymbol(const AnalysisResult& result, SymbolId id)
    {
        return InvalidSymbol != id && id < result.Symbols.Size() ? &result.Symbols[id] : nullptr;
    }

    const DocumentAnalysis* FindDocument(const AnalysisResult& result, std::string_view uri)
    {
        for (const auto& document : result.Documents)
        {
            if (nullptr != document.Get() && document->Input.Uri == uri)
            {
                return document.Get();
            }
        }
        return nullptr;
    }

    ArrayView<const char* const> GetSemanticDiagnosticKeys()
    {
        static constexpr const char* keys[] = {
            "jbroc.semantic.unknown_type", "jbroc.semantic.duplicate", "jbroc.semantic.type_mismatch",
            "jbroc.semantic.unknown_name", "jbroc.semantic.inaccessible", "jbroc.semantic.readonly",
            "jbroc.semantic.null_access", "jbroc.semantic.borrow_position", "jbroc.semantic.handle_nullable",
            "jbroc.semantic.borrow_argument", "jbroc.semantic.return_path", "jbroc.semantic.callback",
            "jbroc.semantic.override", "jbroc.semantic.inheritance_cycle", "jbroc.semantic.inheritance_diamond",
            "jbroc.semantic.abstract_value", "jbroc.semantic.uninitialized", "jbroc.semantic.switch_exhaustive",
            "jbroc.semantic.iteration_mutation", "jbroc.semantic.invalid_value_type", "jbroc.semantic.type_arguments",
            "jbroc.semantic.table_key", "jbroc.semantic.invalid_base", "jbroc.semantic.interface_field",
            "jbroc.semantic.constructor_kind", "jbroc.semantic.abstract_body", "jbroc.semantic.missing_body",
            "jbroc.semantic.static_access", "jbroc.semantic.not_indexable", "jbroc.semantic.not_nullable",
            "jbroc.semantic.operator", "jbroc.semantic.unknown_member", "jbroc.semantic.not_callable",
            "jbroc.semantic.no_overload", "jbroc.semantic.ambiguous_overload", "jbroc.semantic.not_iterable",
            "jbroc.semantic.for_variables", "jbroc.semantic.jump_context", "jbroc.semantic.type_constraint",
            "jbroc.semantic.legacy_nullable_type"
        };
        return {keys, std::size(keys)};
    }

    Array<SymbolId> GetCompletionSymbols(const AnalysisResult& result, std::string_view uri, UInt32 offset)
    {
        Array<SymbolId> candidates;
        const DocumentAnalysis* document = FindDocument(result, uri);
        if (nullptr == document)
        {
            return candidates;
        }
        SymbolId owner = InvalidSymbol;
        SymbolId function = InvalidSymbol;
        for (const Symbol& symbol : result.Symbols)
        {
            if (symbol.Api || symbol.Uri != uri || false == Contains(symbol.Range, offset))
            {
                continue;
            }
            if (IsTypeSymbol(symbol.Kind))
            {
                owner = symbol.Id;
            }
            else if (IsFunction(symbol.Kind))
            {
                function = symbol.Id;
            }
        }
        const std::string_view text = document->Source.GetText();
        std::size_t cursor = std::min(static_cast<std::size_t>(offset), text.size());
        while (cursor > 0 && (('_' == text[cursor - 1]) || ('a' <= text[cursor - 1] && text[cursor - 1] <= 'z')
            || ('A' <= text[cursor - 1] && text[cursor - 1] <= 'Z') || ('0' <= text[cursor - 1] && text[cursor - 1] <= '9')))
        {
            --cursor;
        }
        while (cursor > 0 && (' ' == text[cursor - 1] || '\t' == text[cursor - 1]))
        {
            --cursor;
        }
        if (cursor > 0 && '.' == text[cursor - 1])
        {
            std::size_t receiverEnd = cursor - 1;
            if (receiverEnd > 0 && '?' == text[receiverEnd - 1])
            {
                --receiverEnd;
            }
            while (receiverEnd > 0 && (' ' == text[receiverEnd - 1] || '\t' == text[receiverEnd - 1] || '!' == text[receiverEnd - 1]))
            {
                --receiverEnd;
            }
            String receiverType;
            Bool staticReceiver = false;
            for (NodeIndex index = 0; index < document->Tree.GetNodeCount(); ++index)
            {
                if (document->Tree.Get(index).Range.End.Offset == receiverEnd)
                {
                    const String candidate = ExpressionType(result, *document, index);
                    if (candidate.IsNotEmpty())
                    {
                        receiverType = candidate;
                    }
                }
            }
            for (const Occurrence& occurrence : document->Occurrences)
            {
                if (occurrence.Range.End.Offset == receiverEnd && InvalidSymbol != occurrence.Target)
                {
                    const Symbol& symbol = result.Symbols[occurrence.Target];
                    if (receiverType.IsEmpty())
                    {
                        receiverType = symbol.Type;
                    }
                    staticReceiver = IsTypeSymbol(symbol.Kind);
                }
            }
            if (receiverType.IsEmpty() && receiverEnd >= 4 && text.substr(receiverEnd - 4, 4) == "this" && InvalidSymbol != owner)
            {
                receiverType = result.Symbols[owner].Name;
            }
            for (SymbolId id : Members(result, TypeId(result, receiverType)))
            {
                const Symbol& symbol = result.Symbols[id];
                if (Accessible(result, symbol, owner) && (false == staticReceiver || symbol.IsStatic))
                {
                    candidates.Add(id);
                }
            }
            return candidates;
        }
        Array<SymbolId> locals;
        for (const Symbol& symbol : result.Symbols)
        {
            if ((SymbolKind::Parameter == symbol.Kind || SymbolKind::Local == symbol.Kind) && symbol.Uri == uri
                && symbol.Parent == function && Contains(symbol.ScopeRange, offset) && symbol.SelectionRange.Begin.Offset <= offset)
            {
                locals.Add(symbol.Id);
            }
        }
        std::sort(locals.begin(), locals.end(), [&](SymbolId left, SymbolId right)
        {
            return result.Symbols[left].ScopeRange.Begin.Offset > result.Symbols[right].ScopeRange.Begin.Offset;
        });
        for (SymbolId id : locals)
        {
            if (nullptr == candidates.FindBy([&](SymbolId earlier) { return result.Symbols[earlier].Name == result.Symbols[id].Name; }))
            {
                candidates.Add(id);
            }
        }
        for (SymbolId id : Members(result, owner))
        {
            const Symbol& symbol = result.Symbols[id];
            if (false == Accessible(result, symbol, owner) || (InvalidSymbol != function && result.Symbols[function].IsStatic && false == symbol.IsStatic))
            {
                continue;
            }
            const Bool localShadows = nullptr != locals.FindBy([&](SymbolId local) { return result.Symbols[local].Name == symbol.Name; });
            if (false == localShadows)
            {
                candidates.Add(id);
            }
        }
        for (const Symbol& symbol : result.Symbols)
        {
            if (InvalidSymbol == symbol.Parent && (IsTypeSymbol(symbol.Kind) || SymbolKind::Service == symbol.Kind || SymbolKind::Function == symbol.Kind))
            {
                const Bool shadowed = nullptr != candidates.FindBy([&](SymbolId existing)
                {
                    return result.Symbols[existing].Name == symbol.Name && false == IsFunction(result.Symbols[existing].Kind);
                });
                if (false == shadowed)
                {
                    candidates.Add(symbol.Id);
                }
            }
        }
        return candidates;
    }

    Array<SymbolId> GetRenameSymbols(const AnalysisResult& result, SymbolId id)
    {
        Array<SymbolId> group;
        const Symbol* symbol = FindSymbol(result, id);
        if (nullptr == symbol || symbol->Api)
        {
            return group;
        }
        if (SymbolKind::Constructor == symbol->Kind)
        {
            symbol = FindSymbol(result, symbol->Parent);
            if (nullptr == symbol || symbol->Api || SymbolKind::Class != symbol->Kind)
            {
                return group;
            }
        }
        group.Add(symbol->Id);
        if (SymbolKind::Class == symbol->Kind)
        {
            for (const Symbol& candidate : result.Symbols)
            {
                if (false == candidate.Api && SymbolKind::Constructor == candidate.Kind && candidate.Parent == symbol->Id)
                {
                    group.Add(candidate.Id);
                }
            }
        }
        else if (SymbolKind::Method == symbol->Kind)
        {
            struct ContractEdge { SymbolId Derived; SymbolId Base; };
            Array<ContractEdge> edges;
            for (const Symbol& derived : result.Symbols)
            {
                if (SymbolKind::Method != derived.Kind || derived.IsStatic || InvalidSymbol == derived.Parent)
                {
                    continue;
                }
                Bool explicitOverride = false;
                const DocumentAnalysis* document = FindDocument(result, derived.Uri);
                if (nullptr != document)
                {
                    for (NodeIndex node = 0; node < document->Tree.GetNodeCount(); ++node)
                    {
                        const SyntaxNode& declaration = document->Tree.Get(node);
                        if (SyntaxKind::FunctionDeclaration == declaration.Kind
                            && declaration.Range.Begin.Offset == derived.Range.Begin.Offset
                            && declaration.Range.End.Offset == derived.Range.End.Offset)
                        {
                            explicitOverride = 0 != (declaration.Flags & SyntaxFlagOverride);
                            break;
                        }
                    }
                }
                for (const String& baseType : result.Symbols[derived.Parent].BaseTypes)
                {
                    // Existing member lookup preserves the nearest declaration and hiding semantics.
                    const Array<SymbolId> bases = Members(result, TypeId(result, baseType), derived.Name);
                    for (SymbolId baseId : bases)
                    {
                        const Symbol& base = result.Symbols[baseId];
                        if (SymbolKind::Method != base.Kind || base.IsStatic || base.IsPrivate
                            || false == SameParameters(derived, base) || false == EqualType(derived.Type, base.Type)
                            || derived.TypeParameters.Size() != base.TypeParameters.Size())
                        {
                            continue;
                        }
                        if (explicitOverride || (derived.IsCallback && base.IsCallback) || base.IsAbstract
                            || (InvalidSymbol != base.Parent && SymbolKind::Interface == result.Symbols[base.Parent].Kind))
                        {
                            edges.Add({derived.Id, base.Id});
                        }
                    }
                }
            }
            Bool changed = true;
            while (changed)
            {
                changed = false;
                for (const ContractEdge& edge : edges)
                {
                    if (group.Contains(edge.Derived) && false == group.Contains(edge.Base))
                    {
                        group.Add(edge.Base);
                        changed = true;
                    }
                    if (group.Contains(edge.Base) && false == group.Contains(edge.Derived))
                    {
                        group.Add(edge.Derived);
                        changed = true;
                    }
                }
            }
        }
        return group;
    }

    Bool CanRenameSymbol(const AnalysisResult& result, SymbolId id, std::string_view newName, String& error)
    {
        const Array<SymbolId> group = GetRenameSymbols(result, id);
        const Symbol* symbol = group.IsEmpty() ? nullptr : FindSymbol(result, group.First());
        if (nullptr == symbol)
        {
            error = "Only user symbols can be renamed.";
            return false;
        }
        id = symbol->Id;
        for (SymbolId memberId : group)
        {
            const Symbol& member = result.Symbols[memberId];
            if (member.Api || member.IsCallback)
            {
                error = "This rename group includes an engine API or callback contract whose name is fixed.";
                return false;
            }
        }
        if (newName.empty() || false == (('_' == newName[0]) || ('a' <= newName[0] && newName[0] <= 'z') || ('A' <= newName[0] && newName[0] <= 'Z')))
        {
            error = "The new name must be an ASCII identifier.";
            return false;
        }
        for (char c : newName)
        {
            if (false == (('_' == c) || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9')))
            {
                error = "The new name must be an ASCII identifier.";
                return false;
            }
        }
        if (TokenKind::Identifier != FindKeyword(newName))
        {
            error = "The new name is a reserved keyword.";
            return false;
        }
        for (SymbolId memberId : group)
        {
            const Symbol& member = result.Symbols[memberId];
            if (SymbolKind::Method == member.Kind && InvalidSymbol != member.Parent
                && result.Symbols[member.Parent].Name == newName)
            {
                error = "The new method name would change a method into a constructor.";
                return false;
            }
            for (const Symbol& other : result.Symbols)
            {
                if (group.Contains(other.Id))
                {
                    continue;
                }
                if (SymbolKind::Class == member.Kind && other.Parent == member.Id && other.Name == newName)
                {
                    error = "The new class name conflicts with an existing member declaration.";
                    return false;
                }
                if (other.Name != newName || other.Parent != member.Parent)
                {
                    continue;
                }
                if ((SymbolKind::Local == member.Kind || SymbolKind::Parameter == member.Kind)
                    && false == (Contains(other.ScopeRange, member.SelectionRange.Begin.Offset)
                        || Contains(member.ScopeRange, other.SelectionRange.Begin.Offset)))
                {
                    continue;
                }
                if (IsFunction(other.Kind) && IsFunction(member.Kind) && false == SameParameters(other, member))
                {
                    continue;
                }
                error = "The new name conflicts with an existing declaration.";
                return false;
            }
        }
        for (const auto& document : result.Documents)
        {
            for (const Occurrence& occurrence : document->Occurrences)
            {
                if (false == group.Contains(occurrence.Target))
                {
                    continue;
                }
                const Array<SymbolId> visible = GetCompletionSymbols(result, document->Input.Uri, occurrence.Range.Begin.Offset);
                for (SymbolId candidate : visible)
                {
                    if (false == group.Contains(candidate) && result.Symbols[candidate].Name == newName)
                    {
                        error = "The new name would resolve to another visible symbol.";
                        return false;
                    }
                }
            }
        }
        error.Clear();
        return true;
    }

    const AnalysisResult& AnalysisSession::Update(ArrayView<const DocumentInput> documents, const ApiCatalog& api)
    {
        String signature;
        const auto appendMember = [&](const ApiMember& member)
        {
            signature.Append(member.Name).Append(":").Append(member.Type).Append(":").Append(member.DefinitionUri)
                .Append(":").Append(std::to_string(static_cast<std::uint32_t>(member.DefinitionRange.Begin.Offset)));
            for (const ParameterInfo& parameter : member.Parameters)
            {
                signature.Append(";").Append(parameter.Name).Append(":").Append(parameter.Type)
                    .Append(parameter.Borrowed ? ":ref" : ":value")
                    .Append(parameter.HasDefault ? ":default=" : ":required=").Append(parameter.DefaultValue);
            }
            for (const String& parameter : member.TypeParameters)
            {
                signature.Append("<").Append(parameter).Append(">");
            }
            signature.Append(member.IsStatic ? ":static" : ":instance")
                .Append(member.Writable ? ":write" : ":readonly")
                .Append(member.IsCallback ? ":callback" : ":ordinary")
                .Append(member.MutatesReceiver ? ":mutates" : ":pure")
                .Append(member.MayInvalidateHandles ? ":invalidates" : ":preserves").Append("\n");
        };
        for (const ApiType& type : api.Types)
        {
            signature.Append(type.Name).Append(":").Append(std::to_string(static_cast<unsigned char>(type.Kind)))
                .Append(":").Append(type.DefinitionUri).Append("\n");
            for (const String& base : type.Bases)
            {
                signature.Append(":base=").Append(base).Append("\n");
            }
            for (const ApiMember& member : type.Members)
            {
                appendMember(member);
            }
            for (const String& value : type.EnumValues)
            {
                signature.Append(":enum=").Append(value).Append("\n");
            }
        }
        for (const ApiGlobal& global : api.Globals)
        {
            signature.Append(":global=").Append(global.Name).Append(":").Append(global.Type).Append("\n");
        }
        for (const ApiMember& function : api.Functions)
        {
            appendMember(function);
        }
        if (signature != m_apiSignature)
        {
            m_result = AnalyzeProject(documents, api);
            m_apiSignature = std::move(signature);
        }
        else
        {
            m_result = AnalyzeProjectIncremental(documents, api, std::move(m_result));
        }
        return *m_result;
    }
}
