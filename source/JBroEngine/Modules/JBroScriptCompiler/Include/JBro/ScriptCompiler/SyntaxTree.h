#pragma once

#include <JBro/ScriptCompiler/SourceText.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/String.h>

#include <cstdint>
#include <string_view>

namespace JBro::ScriptCompiler
{
    // 구문 트리 노드의 종류다. 노드마다 자식 자리가 정해져 있고, 빠진 자리는 `InvalidNode` 다.
    // 자리의 뜻은 각 종류 옆에 적는다.
    enum class SyntaxKind : std::uint16_t
    {
        CompilationUnit,        // 선언...

        // 선언
        ScriptDeclaration,      // Text=이름. [BaseList?] 다음 멤버...
        ClassDeclaration,       // 같다
        StructDeclaration,      // 같다
        InterfaceDeclaration,   // 같다
        EnumDeclaration,        // Text=이름. EnumMember...
        EnumMember,             // Text=이름
        BaseList,               // TypeName...
        AttributeList,          // Attribute...
        Attribute,              // Text=이름. 인자 식...
        FieldDeclaration,       // Text=이름, Flags=접근자·static. [AttributeList?, TypeName, 초깃값?]
        FunctionDeclaration,    // Text=이름, Flags=접근자·static·접미사. [AttributeList?, ParameterList, 반환 TypeName?, Block?]
        ParameterList,          // Parameter...
        Parameter,              // Text=이름. [TypeName]

        // 타입
        TypeName,               // Text=이름, Flags=Ref·Const. 타입 인자 TypeName...

        // 문장
        Block,                  // 문장...
        LocalDeclaration,       // Text=이름. [TypeName, 초깃값?]
        ExpressionStatement,    // [식]
        AssignmentStatement,    // Text=연산자(= += -= *= /=). [대상, 값]
        IfStatement,            // [조건, Block, else(Block 또는 IfStatement)?]
        WhileStatement,         // [조건, Block]
        ForStatement,           // [ForVariable, 둘째 ForVariable?, 순회 식, Block]
        ForVariable,            // Text=이름, Flags=Ref
        SwitchStatement,        // [식, CaseClause 또는 DefaultClause...]
        CaseClause,             // [값 식..., Block] - 마지막 자식이 Block 이다
        DefaultClause,          // [Block]
        ReturnStatement,        // [식?]
        BreakStatement,
        ContinueStatement,

        // 식
        NameExpression,         // Text=이름
        GenericNameExpression,  // Text=이름. 타입 인자 TypeName... (GetComponent<Transform2D>)
        IntegerLiteral,         // Text=원문
        FloatLiteral,           // Text=원문
        StringLiteral,          // Text=원문(따옴표 포함)
        BooleanLiteral,         // Text=true 또는 false
        NullLiteral,
        MemberAccessExpression, // Text=멤버 이름. [대상]
        CallExpression,         // [호출 대상, 인자...]
        IndexExpression,        // [대상, 인덱스]
        RefArgument,            // [식] - 호출하는 쪽의 ref hit
        UnaryExpression,        // Text=연산자(- not). [피연산자]
        BinaryExpression,       // Text=연산자. [왼쪽, 오른쪽]
        IsNullExpression,       // Flags=Negated 면 is not null. [피연산자]
        RangeExpression,        // [시작, 끝]
        ParenthesizedExpression, // [식]

        Count,
    };

    // 노드의 표지다. 한 노드에 여럿이 붙는다.
    enum SyntaxFlag : std::uint16_t
    {
        SyntaxFlagNone = 0,
        SyntaxFlagPublic = 1 << 0,
        SyntaxFlagProtected = 1 << 1,
        SyntaxFlagPrivate = 1 << 2,
        SyntaxFlagStatic = 1 << 3,
        SyntaxFlagConst = 1 << 4,
        SyntaxFlagRef = 1 << 5,
        SyntaxFlagCallback = 1 << 6,
        SyntaxFlagOverride = 1 << 7,
        SyntaxFlagRequire = 1 << 8,
        SyntaxFlagNegated = 1 << 9,
    };

    using NodeIndex = std::uint32_t;
    inline constexpr NodeIndex InvalidNode = static_cast<NodeIndex>(-1);

    struct SyntaxNode
    {
        SyntaxKind Kind = SyntaxKind::CompilationUnit;
        std::uint16_t Flags = SyntaxFlagNone;
        SourceRange Range;
        // 이름·연산자·리터럴의 원문이다. `SourceText` 를 가리킨다.
        std::string_view Text;
        std::uint32_t FirstChild = 0;
        std::uint32_t ChildCount = 0;
    };

    // 파일 하나의 구문 트리다.
    //
    // **노드는 포인터가 아니라 번호로 가리킨다.** 모든 노드가 배열 하나에 있고 자식 번호도 배열 하나에 이어 붙는다.
    // 소유를 따로 챙길 것이 없고, 편집기가 매번 다시 파싱해도 할당이 몇 번뿐이다.
    // 노드의 `Text` 는 원문을 가리키므로 `SourceText` 가 이 트리보다 오래 살아야 한다.
    class SyntaxTree final
    {
    public:
        NodeIndex GetRoot() const noexcept { return m_root; }
        const SyntaxNode& Get(NodeIndex index) const { return m_nodes[index]; }
        ArrayView<const NodeIndex> GetChildren(NodeIndex index) const;
        // 자리 하나를 꺼낸다. 자리가 없거나 비었으면 `InvalidNode` 다.
        NodeIndex GetChild(NodeIndex index, std::size_t slot) const;
        std::size_t GetNodeCount() const noexcept { return m_nodes.Size(); }

        // 파서가 쓴다.
        NodeIndex AddNode(SyntaxKind kind, const SourceRange& range, std::string_view text,
            std::uint16_t flags, ArrayView<const NodeIndex> children);
        void SetRoot(NodeIndex root) noexcept { m_root = root; }
        std::size_t GetChildIndexCount() const noexcept { return m_children.Size(); }
        // 추측으로 읽어 보며 만든 노드를 버린다. 그 뒤에 만든 노드를 가리키는 번호가 남아 있으면 안 된다.
        void Truncate(std::size_t nodeCount, std::size_t childIndexCount);

    private:
        Array<SyntaxNode> m_nodes;
        Array<NodeIndex> m_children;
        NodeIndex m_root = InvalidNode;
    };

    const char* GetSyntaxKindName(SyntaxKind kind) noexcept;

    // 테스트와 `jbroc --dump-tree` 가 쓰는 들여쓴 글이다. 빈 자리는 적지 않는다.
    //
    //     ScriptDeclaration Enemy
    //       FieldDeclaration MaxHp {Public}
    //         TypeName Int
    //         IntegerLiteral 10
    String DumpSyntaxTree(const SyntaxTree& tree);
}
