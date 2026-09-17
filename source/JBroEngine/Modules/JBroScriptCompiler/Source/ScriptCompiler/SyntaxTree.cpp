#include <JBro/ScriptCompiler/SyntaxTree.h>

namespace JBro::ScriptCompiler
{
    ArrayView<const NodeIndex> SyntaxTree::GetChildren(NodeIndex index) const
    {
        const SyntaxNode& node = m_nodes[index];
        if (0 == node.ChildCount)
        {
            return ArrayView<const NodeIndex>();
        }
        return ArrayView<const NodeIndex>(m_children.Data() + node.FirstChild, node.ChildCount);
    }

    NodeIndex SyntaxTree::GetChild(NodeIndex index, std::size_t slot) const
    {
        const SyntaxNode& node = m_nodes[index];
        if (slot >= node.ChildCount)
        {
            return InvalidNode;
        }
        return m_children[node.FirstChild + slot];
    }

    NodeIndex SyntaxTree::AddNode(SyntaxKind kind, const SourceRange& range, std::string_view text,
        std::uint16_t flags, ArrayView<const NodeIndex> children)
    {
        SyntaxNode& node = m_nodes.Emplace();
        node.Kind = kind;
        node.Flags = flags;
        node.Range = range;
        node.Text = text;
        node.FirstChild = static_cast<std::uint32_t>(m_children.Size());
        node.ChildCount = static_cast<std::uint32_t>(children.Size());
        for (NodeIndex child : children)
        {
            m_children.Add(child);
        }
        return static_cast<NodeIndex>(m_nodes.Size() - 1);
    }

    void SyntaxTree::Truncate(std::size_t nodeCount, std::size_t childIndexCount)
    {
        if (nodeCount < m_nodes.Size())
        {
            m_nodes.Resize(nodeCount);
        }
        if (childIndexCount < m_children.Size())
        {
            m_children.Resize(childIndexCount);
        }
    }

    const char* GetSyntaxKindName(SyntaxKind kind) noexcept
    {
        switch (kind)
        {
        case SyntaxKind::CompilationUnit: return "CompilationUnit";
        case SyntaxKind::ScriptDeclaration: return "ScriptDeclaration";
        case SyntaxKind::ClassDeclaration: return "ClassDeclaration";
        case SyntaxKind::StructDeclaration: return "StructDeclaration";
        case SyntaxKind::InterfaceDeclaration: return "InterfaceDeclaration";
        case SyntaxKind::EnumDeclaration: return "EnumDeclaration";
        case SyntaxKind::EnumMember: return "EnumMember";
        case SyntaxKind::BaseList: return "BaseList";
        case SyntaxKind::AttributeList: return "AttributeList";
        case SyntaxKind::Attribute: return "Attribute";
        case SyntaxKind::FieldDeclaration: return "FieldDeclaration";
        case SyntaxKind::FunctionDeclaration: return "FunctionDeclaration";
        case SyntaxKind::ParameterList: return "ParameterList";
        case SyntaxKind::Parameter: return "Parameter";
        case SyntaxKind::TypeName: return "TypeName";
        case SyntaxKind::Block: return "Block";
        case SyntaxKind::LocalDeclaration: return "LocalDeclaration";
        case SyntaxKind::ExpressionStatement: return "ExpressionStatement";
        case SyntaxKind::AssignmentStatement: return "AssignmentStatement";
        case SyntaxKind::IfStatement: return "IfStatement";
        case SyntaxKind::WhileStatement: return "WhileStatement";
        case SyntaxKind::ForStatement: return "ForStatement";
        case SyntaxKind::ForVariable: return "ForVariable";
        case SyntaxKind::SwitchStatement: return "SwitchStatement";
        case SyntaxKind::CaseClause: return "CaseClause";
        case SyntaxKind::DefaultClause: return "DefaultClause";
        case SyntaxKind::ReturnStatement: return "ReturnStatement";
        case SyntaxKind::BreakStatement: return "BreakStatement";
        case SyntaxKind::ContinueStatement: return "ContinueStatement";
        case SyntaxKind::NameExpression: return "NameExpression";
        case SyntaxKind::GenericNameExpression: return "GenericNameExpression";
        case SyntaxKind::IntegerLiteral: return "IntegerLiteral";
        case SyntaxKind::FloatLiteral: return "FloatLiteral";
        case SyntaxKind::StringLiteral: return "StringLiteral";
        case SyntaxKind::BooleanLiteral: return "BooleanLiteral";
        case SyntaxKind::NullLiteral: return "NullLiteral";
        case SyntaxKind::MemberAccessExpression: return "MemberAccessExpression";
        case SyntaxKind::CallExpression: return "CallExpression";
        case SyntaxKind::IndexExpression: return "IndexExpression";
        case SyntaxKind::RefArgument: return "RefArgument";
        case SyntaxKind::UnaryExpression: return "UnaryExpression";
        case SyntaxKind::BinaryExpression: return "BinaryExpression";
        case SyntaxKind::IsNullExpression: return "IsNullExpression";
        case SyntaxKind::RangeExpression: return "RangeExpression";
        case SyntaxKind::ParenthesizedExpression: return "ParenthesizedExpression";
        case SyntaxKind::Count: break;
        }
        return "Unknown";
    }

    namespace
    {
        struct FlagName
        {
            std::uint16_t Flag;
            const char* Name;
        };

        constexpr FlagName FlagNames[] = {
            { SyntaxFlagPublic, "Public" },
            { SyntaxFlagProtected, "Protected" },
            { SyntaxFlagPrivate, "Private" },
            { SyntaxFlagStatic, "Static" },
            // 원문에 쓰는 순서(ref const)대로 적는다.
            { SyntaxFlagRef, "Ref" },
            { SyntaxFlagConst, "Const" },
            { SyntaxFlagCallback, "Callback" },
            { SyntaxFlagOverride, "Override" },
            { SyntaxFlagRequire, "Require" },
            { SyntaxFlagNegated, "Negated" },
        };

        void DumpNode(const SyntaxTree& tree, NodeIndex index, std::size_t depth, String& out)
        {
            const SyntaxNode& node = tree.Get(index);
            for (std::size_t level = 0; level < depth; ++level)
            {
                out.Append("  ");
            }
            out.Append(GetSyntaxKindName(node.Kind));
            if (false == node.Text.empty())
            {
                out.Append(" ");
                out.Append(node.Text);
            }
            if (SyntaxFlagNone != node.Flags)
            {
                out.Append(" {");
                bool first = true;
                for (const FlagName& flag : FlagNames)
                {
                    if (0 == (node.Flags & flag.Flag))
                    {
                        continue;
                    }
                    if (false == first)
                    {
                        out.Append("|");
                    }
                    out.Append(flag.Name);
                    first = false;
                }
                out.Append("}");
            }
            out.Append("\n");
            for (NodeIndex child : tree.GetChildren(index))
            {
                if (InvalidNode == child)
                {
                    continue;
                }
                DumpNode(tree, child, depth + 1, out);
            }
        }
    }

    String DumpSyntaxTree(const SyntaxTree& tree)
    {
        String out;
        if (InvalidNode != tree.GetRoot())
        {
            DumpNode(tree, tree.GetRoot(), 0, out);
        }
        return out;
    }
}
