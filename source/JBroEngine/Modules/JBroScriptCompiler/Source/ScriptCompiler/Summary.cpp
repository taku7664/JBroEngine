#include <JBro/ScriptCompiler/Summary.h>

#include <JBro/ScriptCompiler/Lexer.h>
#include <JBro/ScriptCompiler/Parser.h>

#include <algorithm>

namespace JBro::ScriptCompiler
{
    namespace
    {
        const FieldAttributeDefinition AttributeDefinitions[] = {
            {"prop", "", 0}, {"range", "Range", 2}, {"clamp", "Clamp", 2},
            {"name", "Name", 1}, {"tooltip", "Tooltip", 1}, {"category", "Category", 1},
            {"visiblewhen", "VisibleWhen", 1}, {"readonly", "ReadOnly", 0},
            {"noserialize", "NoSerialize", 0}, {"hidden", "Hidden", 0}
        };

        Bool IsType(SyntaxKind kind)
        {
            return kind == SyntaxKind::ScriptDeclaration || kind == SyntaxKind::ClassDeclaration
                || kind == SyntaxKind::StructDeclaration || kind == SyntaxKind::InterfaceDeclaration
                || kind == SyntaxKind::EnumDeclaration;
        }

        String TypeKeyword(SyntaxKind kind)
        {
            switch (kind)
            {
            case SyntaxKind::ScriptDeclaration: return "script";
            case SyntaxKind::ClassDeclaration: return "class";
            case SyntaxKind::StructDeclaration: return "struct";
            case SyntaxKind::InterfaceDeclaration: return "interface";
            case SyntaxKind::EnumDeclaration: return "enum";
            default: return {};
            }
        }

        SourceRange ByteRange(UInt32 begin, UInt32 end)
        {
            SourceRange range;
            range.Begin.Offset = begin;
            range.End.Offset = end;
            return range;
        }

        SourceRange NameRange(std::string_view source, const SyntaxNode& node)
        {
            if (node.Text.empty())
            {
                return node.Range;
            }
            const UInt32 begin = static_cast<UInt32>(node.Text.data() - source.data());
            return ByteRange(begin, begin + static_cast<UInt32>(node.Text.size()));
        }

        String Slice(std::string_view source, SourceRange range)
        {
            return String(source.substr(range.Begin.Offset, range.End.Offset - range.Begin.Offset));
        }

        String Access(std::uint16_t flags)
        {
            if (flags & SyntaxFlagPublic)
            {
                return "public";
            }
            if (flags & SyntaxFlagProtected)
            {
                return "protected";
            }
            if (flags & SyntaxFlagPrivate)
            {
                return "private";
            }
            return "default";
        }

        SummaryEntry Entry(const AnalysisResult& analysis, const DocumentAnalysis& document, NodeIndex index, Bool isStruct)
        {
            const SyntaxTree& tree = document.Tree;
            const SyntaxNode& node = tree.Get(index);
            const std::string_view text = document.Source.GetText();
            SummaryEntry entry;
            entry.Kind = IsType(node.Kind) ? SummaryEntryKind::Type
                : node.Kind == SyntaxKind::FunctionDeclaration ? SummaryEntryKind::Function : SummaryEntryKind::Field;
            entry.Name = node.Kind == SyntaxKind::FunctionDeclaration ? FunctionName(node) : node.Text;
            entry.Range = node.Range;
            entry.SelectionRange = NameRange(text, node);
            entry.Access = Access(node.Flags);
            entry.IsStatic = (node.Flags & SyntaxFlagStatic) != 0;
            entry.IsCallback = (node.Flags & SyntaxFlagCallback) != 0;
            if (entry.Kind == SummaryEntryKind::Type)
            {
                entry.Type = TypeKeyword(node.Kind);
                entry.Signature = entry.Type + " " + entry.Name;
                for (NodeIndex child : tree.GetChildren(index))
                {
                    if (child == InvalidNode)
                    {
                        continue;
                    }
                    const SyntaxKind kind = tree.Get(child).Kind;
                    if (IsType(kind) || kind == SyntaxKind::FieldDeclaration || kind == SyntaxKind::FunctionDeclaration
                        || kind == SyntaxKind::EnumMember)
                    {
                        entry.Members.Add(Entry(analysis, document, child, node.Kind == SyntaxKind::StructDeclaration));
                    }
                }
                return entry;
            }
            const NodeIndex type = tree.GetChild(index, entry.Kind == SummaryEntryKind::Field ? 1 : 2);
            if (type != InvalidNode)
            {
                entry.Type = Slice(text, tree.Get(type).Range);
                entry.IsConst = (tree.Get(type).Flags & SyntaxFlagConst) != 0;
                entry.IsRef = (tree.Get(type).Flags & SyntaxFlagRef) != 0;
            }
            else if (entry.Kind == SummaryEntryKind::Function)
            {
                entry.Type = "Void";
            }
            for (const Symbol& symbol : analysis.Symbols)
            {
                if (false == symbol.Api && symbol.Uri == document.Input.Uri
                    && symbol.SelectionRange.Begin.Offset == entry.SelectionRange.Begin.Offset)
                {
                    entry.Signature = symbol.Signature;
                    entry.IsCallback = entry.IsCallback || symbol.IsCallback;
                    break;
                }
            }
            if (entry.Signature.IsEmpty())
            {
                entry.Signature = entry.Type + " " + entry.Name;
            }
            const NodeIndex attributes = tree.GetChild(index, 0);
            if (attributes != InvalidNode && tree.Get(attributes).Kind == SyntaxKind::AttributeList)
            {
                for (NodeIndex attribute : tree.GetChildren(attributes))
                {
                    if (attribute == InvalidNode)
                    {
                        continue;
                    }
                    SummaryAttribute value;
                    value.Name = tree.Get(attribute).Text;
                    for (NodeIndex argument : tree.GetChildren(attribute))
                    {
                        if (argument != InvalidNode)
                        {
                            value.Arguments.Add(Slice(text, tree.Get(argument).Range));
                        }
                    }
                    entry.Attributes.Add(std::move(value));
                }
            }
            if (node.Kind == SyntaxKind::FieldDeclaration)
            {
                const NodeIndex initializer = tree.GetChild(index, 2);
                entry.HasInitializer = initializer != InvalidNode;
                if (entry.HasInitializer)
                {
                    entry.Initializer = Slice(text, tree.Get(initializer).Range);
                }
                entry.Effective.Property = isStruct || false == entry.Attributes.IsEmpty();
                for (const SummaryAttribute& attribute : entry.Attributes)
                {
                    entry.Effective.ReadOnly = entry.Effective.ReadOnly || attribute.Name == "readonly";
                    entry.Effective.NoSerialize = entry.Effective.NoSerialize || attribute.Name == "noserialize";
                    entry.Effective.Hidden = entry.Effective.Hidden || attribute.Name == "hidden";
                }
                entry.Effective.ReadOnly = entry.Effective.ReadOnly || (entry.Effective.Property && entry.IsConst);
                entry.Effective.NoSerialize = entry.Effective.NoSerialize
                    || (entry.Effective.Property && (entry.IsConst || entry.IsStatic));
            }
            return entry;
        }

        const SummaryAttribute* Desired(const FieldSettingsChange& change, std::string_view name)
        {
            for (const SummaryAttribute& attribute : change.Attributes)
            {
                if (attribute.Name == name)
                {
                    return &attribute;
                }
            }
            return nullptr;
        }

        String AttributeText(const SummaryAttribute& attribute)
        {
            String text = attribute.Name;
            if (false == attribute.Arguments.IsEmpty())
            {
                text.Append("(");
                for (std::size_t index = 0; index < attribute.Arguments.Size(); ++index)
                {
                    if (index != 0)
                    {
                        text.Append(", ");
                    }
                    text.Append(attribute.Arguments[index]);
                }
                text.Append(")");
            }
            return text;
        }

        Bool ValidExpression(std::string_view expression)
        {
            const String trimmed = String(expression).Trim();
            if (trimmed.IsEmpty())
            {
                return false;
            }
            SourceText source("SummaryExpression.jscript", "class SummaryExpression\n{\nInt value = " + trimmed + "\n}\n");
            DiagnosticList diagnostics;
            const SyntaxTree tree = Parse(source, diagnostics);
            if (diagnostics.HasErrors())
            {
                return false;
            }
            const auto declarations = tree.GetChildren(tree.GetRoot());
            if (declarations.Size() != 1)
            {
                return false;
            }
            const auto members = tree.GetChildren(declarations[0]);
            return members.Size() == 2 && members[0] == InvalidNode
                && tree.Get(members[1]).Kind == SyntaxKind::FieldDeclaration
                && tree.Get(members[1]).Text == "value" && tree.GetChild(members[1], 2) != InvalidNode;
        }

        // Trivia lives in the gaps between lexer tokens. Leave every gap intact,
        // including comments inside parenthesized expressions and attribute lists.
        void ReplaceTokens(const Array<Token>& tokens, SourceRange range, std::string_view replacement,
            Array<SummaryTextEdit>& edits)
        {
            Bool first = true;
            for (const Token& token : tokens)
            {
                if (token.Range.Begin.Offset < range.Begin.Offset || token.Range.End.Offset > range.End.Offset
                    || token.Kind == TokenKind::Newline || token.Kind == TokenKind::EndOfFile)
                {
                    continue;
                }
                edits.Add({token.Range, first ? String(replacement) : String()});
                first = false;
            }
        }

        Bool SameTree(const SyntaxTree& before, NodeIndex oldIndex, const SyntaxTree& after, NodeIndex newIndex, NodeIndex field)
        {
            if (oldIndex == InvalidNode || newIndex == InvalidNode)
            {
                return oldIndex == newIndex;
            }
            const SyntaxNode& oldNode = before.Get(oldIndex);
            const SyntaxNode& newNode = after.Get(newIndex);
            if (oldNode.Kind != newNode.Kind || oldNode.Text != newNode.Text || oldNode.Flags != newNode.Flags)
            {
                return false;
            }
            if (oldIndex == field)
            {
                return SameTree(before, before.GetChild(oldIndex, 1), after, after.GetChild(newIndex, 1), InvalidNode);
            }
            const auto oldChildren = before.GetChildren(oldIndex);
            const auto newChildren = after.GetChildren(newIndex);
            if (oldChildren.Size() != newChildren.Size())
            {
                return false;
            }
            for (std::size_t index = 0; index < oldChildren.Size(); ++index)
            {
                if (false == SameTree(before, oldChildren[index], after, newChildren[index], field))
                {
                    return false;
                }
            }
            return true;
        }

        void EditAttributes(const DocumentAnalysis& document, NodeIndex field, const FieldSettingsChange& change,
            const Array<Token>& tokens, Array<SummaryTextEdit>& edits)
        {
            const SyntaxTree& tree = document.Tree;
            const NodeIndex list = tree.GetChild(field, 0);
            Array<String> present;
            if (list != InvalidNode)
            {
                const auto attributes = tree.GetChildren(list);
                // Each bracket group has one separator between adjacent attributes.
                Array<const Token*> brackets;
                Array<const Token*> separators;
                Array<NodeIndex> group;
                UInt32 depth = 0;
                std::size_t nextAttribute = 0;
                auto finish = [&]
                {
                    Array<Bool> keep;
                    std::size_t remaining = 0;
                    for (NodeIndex attribute : group)
                    {
                        const SyntaxNode& node = tree.Get(attribute);
                        const Bool known = nullptr != FindFieldAttributeDefinition(node.Text);
                        const SummaryAttribute* desired = Desired(change, node.Text);
                        const Bool retained = false == known || nullptr != desired;
                        keep.Add(retained);
                        if (retained)
                        {
                            ++remaining;
                        }
                        if (known)
                        {
                            present.Add(String(node.Text));
                            if (nullptr == desired)
                            {
                                ReplaceTokens(tokens, node.Range, {}, edits);
                            }
                            else if (node.ChildCount == desired->Arguments.Size())
                            {
                                for (std::size_t argument = 0; argument < node.ChildCount; ++argument)
                                {
                                    const SourceRange range = tree.Get(tree.GetChild(attribute, argument)).Range;
                                    if (Slice(document.Source.GetText(), range) != desired->Arguments[argument])
                                    {
                                        ReplaceTokens(tokens, range, desired->Arguments[argument].View(), edits);
                                    }
                                }
                            }
                            else
                            {
                                ReplaceTokens(tokens, node.Range, AttributeText(*desired).View(), edits);
                            }
                        }
                    }
                    if (remaining == 0)
                    {
                        for (const Token* bracket : brackets)
                        {
                            edits.Add({bracket->Range, {}});
                        }
                    }
                    Bool seenKept = false;
                    for (std::size_t index = 0; index < keep.Size(); ++index)
                    {
                        if (index > 0 && index - 1 < separators.Size() && (false == keep[index] || false == seenKept))
                        {
                            edits.Add({separators[index - 1]->Range, {}});
                        }
                        seenKept = seenKept || keep[index];
                    }
                    brackets.Clear();
                    separators.Clear();
                    group.Clear();
                };
                for (const Token& token : tokens)
                {
                    if (token.Range.Begin.Offset < tree.Get(list).Range.Begin.Offset
                        || token.Range.End.Offset > tree.Get(list).Range.End.Offset)
                    {
                        continue;
                    }
                    if (token.Kind == TokenKind::LeftBracket && depth == 0)
                    {
                        brackets.Add(&token);
                        depth = 1;
                        continue;
                    }
                    if (depth == 0)
                    {
                        continue;
                    }
                    if (token.Kind == TokenKind::RightBracket && depth == 1)
                    {
                        brackets.Add(&token);
                        finish();
                        depth = 0;
                        continue;
                    }
                    if (nextAttribute < attributes.Size() && attributes[nextAttribute] != InvalidNode
                        && tree.Get(attributes[nextAttribute]).Range.Begin.Offset == token.Range.Begin.Offset)
                    {
                        group.Add(attributes[nextAttribute]);
                        ++nextAttribute;
                    }
                    if (token.Kind == TokenKind::Comma && depth == 1)
                    {
                        separators.Add(&token);
                    }
                    if (token.Kind == TokenKind::LeftParen || token.Kind == TokenKind::LeftBracket)
                    {
                        ++depth;
                    }
                    else if (token.Kind == TokenKind::RightParen || token.Kind == TokenKind::RightBracket)
                    {
                        --depth;
                    }
                }
            }
            String added;
            for (const SummaryAttribute& attribute : change.Attributes)
            {
                if (false == present.Contains(attribute.Name))
                {
                    added.Append("[").Append(AttributeText(attribute)).Append("] ");
                }
            }
            if (false == added.IsEmpty())
            {
                const SourceLocation begin = tree.Get(field).Range.Begin;
                edits.Add({{begin, begin}, std::move(added)});
            }
        }
    }

    ArrayView<const FieldAttributeDefinition> GetFieldAttributeDefinitions()
    {
        return {AttributeDefinitions, std::size(AttributeDefinitions)};
    }

    const FieldAttributeDefinition* FindFieldAttributeDefinition(std::string_view name)
    {
        for (const FieldAttributeDefinition& definition : AttributeDefinitions)
        {
            if (name == definition.Name)
            {
                return &definition;
            }
        }
        return nullptr;
    }

    ScriptSummary SummarizeScript(const AnalysisResult& analysis, const DocumentAnalysis& document)
    {
        ScriptSummary summary;
        summary.Valid = false == document.ParseDiagnostics.HasErrors() && document.Tree.GetRoot() != InvalidNode;
        if (false == summary.Valid)
        {
            return summary;
        }
        for (NodeIndex index : document.Tree.GetChildren(document.Tree.GetRoot()))
        {
            if (index != InvalidNode && IsType(document.Tree.Get(index).Kind))
            {
                summary.Types.Add(Entry(analysis, document, index, false));
            }
        }
        return summary;
    }

    Bool EditFieldSettings(const DocumentAnalysis& document, UInt32 selectionOffset,
        const FieldSettingsChange& change, Array<SummaryTextEdit>& edits, String& error)
    {
        edits.Clear();
        error.Clear();
        if (document.ParseDiagnostics.HasErrors() || document.Tree.GetRoot() == InvalidNode)
        {
            error = "The document has syntax errors; field settings are unavailable.";
            return false;
        }
        const SyntaxTree& tree = document.Tree;
        const std::string_view source = document.Source.GetText();
        NodeIndex field = InvalidNode;
        for (NodeIndex index = 0; index < tree.GetNodeCount(); ++index)
        {
            const SyntaxNode& node = tree.Get(index);
            if (node.Kind == SyntaxKind::FieldDeclaration && NameRange(source, node).Begin.Offset == selectionOffset)
            {
                field = index;
                break;
            }
        }
        if (field == InvalidNode)
        {
            error = "Select the current field declaration name before editing its settings.";
            return false;
        }
        Array<String> names;
        for (const SummaryAttribute& attribute : change.Attributes)
        {
            const FieldAttributeDefinition* definition = FindFieldAttributeDefinition(attribute.Name.View());
            if (nullptr == definition || definition->ArgumentCount != attribute.Arguments.Size() || names.Contains(attribute.Name))
            {
                error = "A field attribute is unknown, duplicated, or has an incorrect argument count.";
                return false;
            }
            names.Add(attribute.Name);
            for (const String& argument : attribute.Arguments)
            {
                if (false == ValidExpression(argument.View()))
                {
                    error = "An attribute argument must be one complete expression.";
                    return false;
                }
            }
        }
        if (change.ChangeInitializer && change.HasInitializer && false == ValidExpression(change.Initializer.View()))
        {
            error = "The initializer must be one complete expression.";
            return false;
        }
        DiagnosticList diagnostics;
        const Array<Token> tokens = Lex(document.Source, diagnostics);
        if (change.ChangeAttributes)
        {
            EditAttributes(document, field, change, tokens, edits);
        }
        if (change.ChangeInitializer)
        {
            const NodeIndex initializer = tree.GetChild(field, 2);
            if (initializer != InvalidNode)
            {
                const SourceRange range = tree.Get(initializer).Range;
                if (false == change.HasInitializer)
                {
                    const SourceRange name = NameRange(source, tree.Get(field));
                    ReplaceTokens(tokens, ByteRange(name.End.Offset, range.End.Offset), {}, edits);
                }
                else if (Slice(source, range) != change.Initializer)
                {
                    ReplaceTokens(tokens, range, change.Initializer.View(), edits);
                }
            }
            else if (change.HasInitializer)
            {
                const SourceLocation end = tree.Get(field).Range.End;
                edits.Add({{end, end}, " = " + change.Initializer});
            }
        }
        // Merge insertions at the beginning of a deleted token into that replacement.
        // This makes every returned edit disjoint for LSP clients.
        std::sort(edits.begin(), edits.end(), [](const SummaryTextEdit& left, const SummaryTextEdit& right)
        {
            if (left.Range.Begin.Offset != right.Range.Begin.Offset)
            {
                return left.Range.Begin.Offset < right.Range.Begin.Offset;
            }
            return left.Range.End.Offset < right.Range.End.Offset;
        });
        Array<SummaryTextEdit> merged;
        for (const SummaryTextEdit& edit : edits)
        {
            if (false == merged.IsEmpty() && merged.Last().Range.Begin.Offset == edit.Range.Begin.Offset
                && merged.Last().Range.End.Offset == merged.Last().Range.Begin.Offset)
            {
                merged.Last().Range.End = edit.Range.End;
                merged.Last().NewText.Append(edit.NewText);
            }
            else
            {
                merged.Add(edit);
            }
        }
        edits = std::move(merged);
        String candidate(source);
        for (std::size_t index = edits.Size(); index > 0; --index)
        {
            const SummaryTextEdit& edit = edits[index - 1];
            candidate.replace(edit.Range.Begin.Offset, edit.Range.End.Offset - edit.Range.Begin.Offset, edit.NewText);
        }
        const SourceText candidateSource(document.Input.Path, std::move(candidate));
        DiagnosticList candidateDiagnostics;
        const SyntaxTree candidateTree = Parse(candidateSource, candidateDiagnostics);
        if (candidateDiagnostics.HasErrors() || false == SameTree(tree, tree.GetRoot(), candidateTree, candidateTree.GetRoot(), field))
        {
            edits.Clear();
            error = "The requested settings cannot safely replace this declaration without changing other code.";
            return false;
        }
        return true;
    }
}
