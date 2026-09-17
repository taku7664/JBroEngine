// jbroc 의 파서를 찌른다(D-104).
//
// 트리는 들여쓴 덤프 글로 바꿔 비교한다. 틀렸을 때 기대와 실제를 나란히 보이기 위해서다.
// 문법 문서(jbroscript-syntax.md)의 모양마다 하나씩, 그리고 에러는 "무엇을 어디서 내는가" 를 본다.

#include <JBro/ScriptCompiler/Parser.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace
{
    using namespace JBro;
    using namespace JBro::ScriptCompiler;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    struct ParseResult
    {
        explicit ParseResult(const std::string& text)
            : Source(String("test.jscript"), String(text))
        {
            Tree = Parse(Source, Diagnostics);
        }

        SourceText Source;
        DiagnosticList Diagnostics;
        SyntaxTree Tree;
    };

    void PrintDiagnostics(const ParseResult& result)
    {
        for (const Diagnostic& diagnostic : result.Diagnostics.GetItems())
        {
            std::cout << "    " << diagnostic.Range.Begin.Line << ':' << diagnostic.Range.Begin.Column
                << ' ' << GetDiagnosticKey(diagnostic.Code) << '\n';
        }
    }

    // 원문을 파싱해 에러가 없고 덤프가 기대와 같은지 본다.
    void CheckTree(const std::string& text, const std::string& expected, const char* message)
    {
        ParseResult result(text);
        const std::string actual = DumpSyntaxTree(result.Tree).Std();
        if (actual != expected || result.Diagnostics.GetCount() != 0)
        {
            std::cout << "  source:\n" << text << "\n  expected:\n" << expected << "  actual:\n" << actual;
            PrintDiagnostics(result);
        }
        Check(result.Diagnostics.GetCount() == 0, message);
        Check(actual == expected, message);
    }

    // 함수 본문 하나를 감싸 파싱하고, 본문 블록의 덤프만 비교한다.
    std::string InFunction(const std::string& body)
    {
        return "script P\n{\n    fn F()\n    {\n" + body + "    }\n}\n";
    }

    const char* const FunctionPrefix =
        "CompilationUnit\n"
        "  ScriptDeclaration P\n"
        "    FunctionDeclaration F\n"
        "      ParameterList\n";

    void CheckBody(const std::string& body, const std::string& expectedBlock, const char* message)
    {
        CheckTree(InFunction(body), std::string(FunctionPrefix) + expectedBlock, message);
    }

    // 에러가 정확히 하나이고 그 종류와 줄·열이 기대와 같은지 본다.
    void CheckOneError(const std::string& text, DiagnosticCode code, std::uint32_t line, std::uint32_t column,
        const char* message)
    {
        ParseResult result(text);
        const Array<Diagnostic>& items = result.Diagnostics.GetItems();
        const bool matches = items.Size() == 1 && items[0].Code == code
            && items[0].Range.Begin.Line == line && items[0].Range.Begin.Column == column;
        if (false == matches)
        {
            std::cout << "  source:\n" << text << "\n  expected " << GetDiagnosticKey(code) << " at "
                << line << ':' << column << ", got " << items.Size() << ":\n";
            PrintDiagnostics(result);
        }
        Check(matches, message);
    }

    std::size_t CountLines(const std::string& text)
    {
        std::size_t lines = 0;
        for (char c : text)
        {
            if ('\n' == c)
            {
                ++lines;
            }
        }
        return lines;
    }

    void TestEmptyFile()
    {
        CheckTree("", "CompilationUnit\n", "an empty file is an empty compilation unit");
        CheckTree("\n\n// only a comment\n\n", "CompilationUnit\n", "blank lines and comments are nothing");
    }

    void TestTypeDeclarationsAndFields()
    {
        CheckTree(
            "script Enemy : IDamageable, IHealable\n"
            "{\n"
            "    [range(1, 100), category(\"Stats\")]\n"
            "    public Int MaxHp = 10\n"
            "    [prop] Bool ShowDebug = false\n"
            "    ref Transform2D target\n"
            "    Array<ref Enemy> allies\n"
            "    Table<String, Array<Float>> curves\n"
            "    protected static const Float Gravity = 9.8\n"
            "    private Int secret\n"
            "}\n",
            "CompilationUnit\n"
            "  ScriptDeclaration Enemy\n"
            "    BaseList\n"
            "      TypeName IDamageable\n"
            "      TypeName IHealable\n"
            "    FieldDeclaration MaxHp {Public}\n"
            "      AttributeList\n"
            "        Attribute range\n"
            "          IntegerLiteral 1\n"
            "          IntegerLiteral 100\n"
            "        Attribute category\n"
            "          StringLiteral \"Stats\"\n"
            "      TypeName Int\n"
            "      IntegerLiteral 10\n"
            "    FieldDeclaration ShowDebug\n"
            "      AttributeList\n"
            "        Attribute prop\n"
            "      TypeName Bool\n"
            "      BooleanLiteral false\n"
            "    FieldDeclaration target\n"
            "      TypeName Transform2D {Ref}\n"
            "    FieldDeclaration allies\n"
            "      TypeName Array\n"
            "        TypeName Enemy {Ref}\n"
            "    FieldDeclaration curves\n"
            "      TypeName Table\n"
            "        TypeName String\n"
            "        TypeName Array\n"
            "          TypeName Float\n"
            "    FieldDeclaration Gravity {Protected|Static}\n"
            "      TypeName Float {Const}\n"
            "      FloatLiteral 9.8\n"
            "    FieldDeclaration secret {Private}\n"
            "      TypeName Int\n",
            "a script with parents, attributes, modifiers, ref and nested generic fields");

        CheckTree(
            "class Inventory\n{\n}\n"
            "struct DropEntry\n{\n    Int Weight = 1\n}\n"
            "interface ICombatant : IDamageable, IHealable\n{\n}\n"
            "enum EnemyState\n{\n    Idle\n    Chasing   // one per line\n    Dead\n}\n",
            "CompilationUnit\n"
            "  ClassDeclaration Inventory\n"
            "  StructDeclaration DropEntry\n"
            "    FieldDeclaration Weight\n"
            "      TypeName Int\n"
            "      IntegerLiteral 1\n"
            "  InterfaceDeclaration ICombatant\n"
            "    BaseList\n"
            "      TypeName IDamageable\n"
            "      TypeName IHealable\n"
            "  EnumDeclaration EnemyState\n"
            "    EnumMember Idle\n"
            "    EnumMember Chasing\n"
            "    EnumMember Dead\n",
            "every declaration kind");

        // 문맥 키워드와 C++ 키워드는 이름이다(jbroscript-syntax §2.1).
        CheckTree(
            "struct Item\n{\n    String template\n    Int delete = 0\n    Int override = 1\n    Int in = 2\n}\n",
            "CompilationUnit\n"
            "  StructDeclaration Item\n"
            "    FieldDeclaration template\n"
            "      TypeName String\n"
            "    FieldDeclaration delete\n"
            "      TypeName Int\n"
            "      IntegerLiteral 0\n"
            "    FieldDeclaration override\n"
            "      TypeName Int\n"
            "      IntegerLiteral 1\n"
            "    FieldDeclaration in\n"
            "      TypeName Int\n"
            "      IntegerLiteral 2\n",
            "contextual words and C++ keywords are field names");
    }

    void TestFunctions()
    {
        CheckTree(
            "interface IDamageable\n"
            "{\n"
            "    fn TakeDamage(Int amount)\n"
            "    fn IsDead() -> Bool\n"
            "}\n"
            "class Weapon\n"
            "{\n"
            "    fn Damage() -> Int require\n"
            "    fn OnStart() callback\n"
            "    {\n"
            "    }\n"
            "    public static fn Sum(ref const Array<DropEntry> table, Int start) -> ref Enemy override\n"
            "    {\n"
            "    }\n"
            "    fn Inventory(Int startGold)\n"
            "    {\n"
            "    }\n"
            "}\n",
            "CompilationUnit\n"
            "  InterfaceDeclaration IDamageable\n"
            "    FunctionDeclaration TakeDamage\n"
            "      ParameterList\n"
            "        Parameter amount\n"
            "          TypeName Int\n"
            "    FunctionDeclaration IsDead\n"
            "      ParameterList\n"
            "      TypeName Bool\n"
            "  ClassDeclaration Weapon\n"
            "    FunctionDeclaration Damage {Require}\n"
            "      ParameterList\n"
            "      TypeName Int\n"
            "    FunctionDeclaration OnStart {Callback}\n"
            "      ParameterList\n"
            "      Block\n"
            "    FunctionDeclaration Sum {Public|Static|Override}\n"
            "      ParameterList\n"
            "        Parameter table\n"
            "          TypeName Array {Ref|Const}\n"
            "            TypeName DropEntry\n"
            "        Parameter start\n"
            "          TypeName Int\n"
            "      TypeName Enemy {Ref}\n"
            "      Block\n"
            "    FunctionDeclaration Inventory\n"
            "      ParameterList\n"
            "        Parameter startGold\n"
            "          TypeName Int\n"
            "      Block\n",
            "functions with and without bodies, return types, suffixes and a constructor");
    }

    void TestDeclarationsAreToldFromExpressionsByShape()
    {
        CheckBody(
            "        Collision2D hit\n"
            "        Array<ref Enemy> allies\n"
            "        ref Enemy nearest = FindNearest()\n"
            "        Float hp = enemy.GetHp()\n"
            "        hp -= amount\n"
            "        target.position.x += MoveSpeed * dt\n"
            "        Die()\n"
            "        return\n",
            "      Block\n"
            "        LocalDeclaration hit\n"
            "          TypeName Collision2D\n"
            "        LocalDeclaration allies\n"
            "          TypeName Array\n"
            "            TypeName Enemy {Ref}\n"
            "        LocalDeclaration nearest\n"
            "          TypeName Enemy {Ref}\n"
            "          CallExpression\n"
            "            NameExpression FindNearest\n"
            "        LocalDeclaration hp\n"
            "          TypeName Float\n"
            "          CallExpression\n"
            "            MemberAccessExpression GetHp\n"
            "              NameExpression enemy\n"
            "        AssignmentStatement -=\n"
            "          NameExpression hp\n"
            "          NameExpression amount\n"
            "        AssignmentStatement +=\n"
            "          MemberAccessExpression x\n"
            "            MemberAccessExpression position\n"
            "              NameExpression target\n"
            "          BinaryExpression *\n"
            "            NameExpression MoveSpeed\n"
            "            NameExpression dt\n"
            "        ExpressionStatement\n"
            "          CallExpression\n"
            "            NameExpression Die\n"
            "        ReturnStatement\n",
            "an engine type the parser has never heard of still starts a declaration");

        // `a < b` 로 시작하는 문장은 타입으로 읽어 보다가 실패하면 식이다. 읽어 본 흔적이 트리에 남으면 안 된다.
        // `a < b` 로 시작하는 문장은 선언으로 읽어 보다 실패한다. 읽어 보는 동안의 에러가 새면 안 된다.
        // `a < b > c` 는 `>` 뒤에 `(` 가 없으므로 제네릭 호출이 아니라 비교의 연쇄다.
        ParseResult result(InFunction(
            "        ok = a < b\n"
            "        c = GetComponent<Transform2D>()\n"
            "        a < b\n"
            "        ok = a < b > c\n"));
        if (result.Diagnostics.GetCount() != 0)
        {
            PrintDiagnostics(result);
        }
        Check(result.Diagnostics.GetCount() == 0, "comparisons and a generic call parse, and guessing leaks no errors");
        const std::string dump = DumpSyntaxTree(result.Tree).Std();
        Check(dump == std::string(FunctionPrefix)
            + "      Block\n"
              "        AssignmentStatement =\n"
              "          NameExpression ok\n"
              "          BinaryExpression <\n"
              "            NameExpression a\n"
              "            NameExpression b\n"
              "        AssignmentStatement =\n"
              "          NameExpression c\n"
              "          CallExpression\n"
              "            GenericNameExpression GetComponent\n"
              "              TypeName Transform2D\n"
              "        ExpressionStatement\n"
              "          BinaryExpression <\n"
              "            NameExpression a\n"
              "            NameExpression b\n"
              "        AssignmentStatement =\n"
              "          NameExpression ok\n"
              "          BinaryExpression >\n"
              "            BinaryExpression <\n"
              "              NameExpression a\n"
              "              NameExpression b\n"
              "            NameExpression c\n",
            "a < b is a comparison, GetComponent<Transform2D>() is a generic call and a < b > c is a chain");
        Check(result.Tree.GetNodeCount() == CountLines(dump),
            "nodes built while guessing are thrown away, so every node is reachable from the root");
        std::size_t childSlots = 0;
        for (NodeIndex node = 0; node < result.Tree.GetNodeCount(); ++node)
        {
            childSlots += result.Tree.Get(node).ChildCount;
        }
        Check(result.Tree.GetChildIndexCount() == childSlots,
            "child indices built while guessing are thrown away too, so none are left unowned");
    }

    void TestOperatorPrecedence()
    {
        CheckBody(
            "        x = a + b * c - d\n"
            "        ok = hp <= 0 and not isDead or target is not null\n"
            "        y = -a.b\n"
            "        v = -a * b\n"
            "        z = not ally.IsDead()\n"
            "        r = Float(hp) / Float(MaxHp)\n"
            "        w = (a + b) % c\n"
            "        e = state == EnemyState.Dead\n",
            "      Block\n"
            "        AssignmentStatement =\n"
            "          NameExpression x\n"
            "          BinaryExpression -\n"
            "            BinaryExpression +\n"
            "              NameExpression a\n"
            "              BinaryExpression *\n"
            "                NameExpression b\n"
            "                NameExpression c\n"
            "            NameExpression d\n"
            "        AssignmentStatement =\n"
            "          NameExpression ok\n"
            "          BinaryExpression or\n"
            "            BinaryExpression and\n"
            "              BinaryExpression <=\n"
            "                NameExpression hp\n"
            "                IntegerLiteral 0\n"
            "              UnaryExpression not\n"
            "                NameExpression isDead\n"
            "            IsNullExpression {Negated}\n"
            "              NameExpression target\n"
            "        AssignmentStatement =\n"
            "          NameExpression y\n"
            "          UnaryExpression -\n"
            "            MemberAccessExpression b\n"
            "              NameExpression a\n"
            "        AssignmentStatement =\n"
            "          NameExpression v\n"
            "          BinaryExpression *\n"
            "            UnaryExpression -\n"
            "              NameExpression a\n"
            "            NameExpression b\n"
            "        AssignmentStatement =\n"
            "          NameExpression z\n"
            "          UnaryExpression not\n"
            "            CallExpression\n"
            "              MemberAccessExpression IsDead\n"
            "                NameExpression ally\n"
            "        AssignmentStatement =\n"
            "          NameExpression r\n"
            "          BinaryExpression /\n"
            "            CallExpression\n"
            "              NameExpression Float\n"
            "              NameExpression hp\n"
            "            CallExpression\n"
            "              NameExpression Float\n"
            "              NameExpression MaxHp\n"
            "        AssignmentStatement =\n"
            "          NameExpression w\n"
            "          BinaryExpression %\n"
            "            ParenthesizedExpression\n"
            "              BinaryExpression +\n"
            "                NameExpression a\n"
            "                NameExpression b\n"
            "            NameExpression c\n"
            "        AssignmentStatement =\n"
            "          NameExpression e\n"
            "          BinaryExpression ==\n"
            "            NameExpression state\n"
            "            MemberAccessExpression Dead\n"
            "              NameExpression EnemyState\n",
            "precedence follows jbroscript-syntax 10.1");
    }

    void TestControlFlow()
    {
        CheckBody(
            "        if (hp <= 0)\n"
            "        {\n"
            "            Die()\n"
            "        }\n"
            "        else if (hp < 10)\n"
            "        {\n"
            "        }\n"
            "        else\n"
            "        {\n"
            "        }\n"
            "        while (timer > 0.0)\n"
            "        {\n"
            "            break\n"
            "        }\n"
            "        if (x is null)\n"
            "        {\n"
            "            continue\n"
            "        }\n",
            "      Block\n"
            "        IfStatement\n"
            "          BinaryExpression <=\n"
            "            NameExpression hp\n"
            "            IntegerLiteral 0\n"
            "          Block\n"
            "            ExpressionStatement\n"
            "              CallExpression\n"
            "                NameExpression Die\n"
            "          IfStatement\n"
            "            BinaryExpression <\n"
            "              NameExpression hp\n"
            "              IntegerLiteral 10\n"
            "            Block\n"
            "            Block\n"
            "        WhileStatement\n"
            "          BinaryExpression >\n"
            "            NameExpression timer\n"
            "            FloatLiteral 0.0\n"
            "          Block\n"
            "            BreakStatement\n"
            "        IfStatement\n"
            "          IsNullExpression\n"
            "            NameExpression x\n"
            "          Block\n"
            "            ContinueStatement\n",
            "if, else if, else, while, break, continue and is null");

        CheckBody(
            "        for (i in 0..count)\n"
            "        {\n"
            "        }\n"
            "        for (ref entry in drops)\n"
            "        {\n"
            "        }\n"
            "        for (key, value in scores)\n"
            "        {\n"
            "        }\n"
            "        for (i in 0..table.Size())\n"
            "        {\n"
            "            total += table[i].Weight\n"
            "        }\n",
            "      Block\n"
            "        ForStatement\n"
            "          ForVariable i\n"
            "          RangeExpression\n"
            "            IntegerLiteral 0\n"
            "            NameExpression count\n"
            "          Block\n"
            "        ForStatement\n"
            "          ForVariable entry {Ref}\n"
            "          NameExpression drops\n"
            "          Block\n"
            "        ForStatement\n"
            "          ForVariable key\n"
            "          ForVariable value\n"
            "          NameExpression scores\n"
            "          Block\n"
            "        ForStatement\n"
            "          ForVariable i\n"
            "          RangeExpression\n"
            "            IntegerLiteral 0\n"
            "            CallExpression\n"
            "              MemberAccessExpression Size\n"
            "                NameExpression table\n"
            "          Block\n"
            "            AssignmentStatement +=\n"
            "              NameExpression total\n"
            "              MemberAccessExpression Weight\n"
            "                IndexExpression\n"
            "                  NameExpression table\n"
            "                  NameExpression i\n",
            "the three for shapes, a range over a call, and indexing");

        CheckBody(
            "        switch (state)\n"
            "        {\n"
            "            case EnemyState.Idle, EnemyState.Dead\n"
            "            {\n"
            "                return\n"
            "            }\n"
            "            default\n"
            "            {\n"
            "            }\n"
            "        }\n",
            "      Block\n"
            "        SwitchStatement\n"
            "          NameExpression state\n"
            "          CaseClause\n"
            "            MemberAccessExpression Idle\n"
            "              NameExpression EnemyState\n"
            "            MemberAccessExpression Dead\n"
            "              NameExpression EnemyState\n"
            "            Block\n"
            "              ReturnStatement\n"
            "          DefaultClause\n"
            "            Block\n",
            "switch with a two-value case and default");

        CheckBody(
            "        Bool found = Physics2D.Raycast(from,\n"
            "            down,\n"
            "            1.0, ref hit)\n"
            "        return found\n",
            "      Block\n"
            "        LocalDeclaration found\n"
            "          TypeName Bool\n"
            "          CallExpression\n"
            "            MemberAccessExpression Raycast\n"
            "              NameExpression Physics2D\n"
            "            NameExpression from\n"
            "            NameExpression down\n"
            "            FloatLiteral 1.0\n"
            "            RefArgument\n"
            "              NameExpression hit\n"
            "        ReturnStatement\n"
            "          NameExpression found\n",
            "a call split over lines with a ref argument");
    }

    void TestRangesCoverTheirSource()
    {
        ParseResult result(
            "script P\n"
            "{\n"
            "    [prop]\n"
            "    Int Speed = 3\n"
            "    fn Ping() -> Int require\n"
            "\n"
            "    fn F()\n"
            "    {\n"
            "        if (x)\n"
            "        {\n"
            "        }\n"
            "\n"
            "        Go()\n"
            "    }\n"
            "}\n");
        Check(result.Diagnostics.GetCount() == 0, "the range probe parses");
        const NodeIndex script = result.Tree.GetChild(result.Tree.GetRoot(), 0);
        const NodeIndex field = result.Tree.GetChild(script, 1);
        const SyntaxNode& node = result.Tree.Get(field);
        Check(SyntaxKind::FieldDeclaration == node.Kind, "the second child of the script is the field");
        Check(node.Range.Begin.Line == 3 && node.Range.Begin.Column == 5,
            "a field range starts at its attribute, so an error on the whole field covers it");
        Check(node.Range.End.Line == 4 && node.Range.End.Column == 18,
            "a field range ends after its initializer, not on the next line");

        // 본문이 없는 함수와 else 가 없는 if 는 뒤의 빈 줄을 들여다본다. 들여다본 줄이 범위에 들어가면 안 된다.
        const SyntaxNode& bodiless = result.Tree.Get(result.Tree.GetChild(script, 2));
        Check(SyntaxKind::FunctionDeclaration == bodiless.Kind && bodiless.Text == "Ping", "the third child is Ping");
        Check(bodiless.Range.End.Line == 5 && bodiless.Range.End.Column == 29,
            "a function without a body ends at its suffix, not at the next declaration");
        const NodeIndex function = result.Tree.GetChild(script, 3);
        const NodeIndex body = result.Tree.GetChild(function, 3);
        const SyntaxNode& ifStatement = result.Tree.Get(result.Tree.GetChild(body, 0));
        Check(SyntaxKind::IfStatement == ifStatement.Kind, "the first statement is the if");
        Check(ifStatement.Range.End.Line == 11 && ifStatement.Range.End.Column == 10,
            "an if without else ends at its closing brace, not at the next statement");
    }

    void TestErrors()
    {
        CheckOneError(InFunction("        if hp <= 0\n        {\n        }\n"),
            DiagnosticCode::MissingConditionParentheses, 5, 12, "if without parentheses");
        CheckOneError(InFunction("        if let box = FieldBox\n        {\n        }\n"),
            DiagnosticCode::MissingConditionParentheses, 5, 12, "the old if let is an if without parentheses");
        CheckOneError(InFunction("        if (x) return\n"),
            DiagnosticCode::ExpectedBlock, 5, 16, "a single-line body");
        CheckOneError("script P\n{\n    public MaxHp = 10\n}\n",
            DiagnosticCode::MissingFieldType, 3, 12, "a field without a type");
        CheckOneError(InFunction("        ok = x is 3\n"),
            DiagnosticCode::ExpectedNullAfterIs, 5, 19, "is must be followed by null");
        CheckOneError(InFunction("        for (i 0..n)\n        {\n        }\n"),
            DiagnosticCode::ExpectedIn, 5, 16, "for without in");
        CheckOneError(InFunction("        a = 1 b = 2\n"),
            DiagnosticCode::ExpectedEndOfStatement, 5, 15, "two statements on one line");
        CheckOneError("script P\n{\n    fn F(Int a\n    {\n    }\n}\n",
            DiagnosticCode::ExpectedToken, 4, 5, "an unclosed parameter list reports at the brace, because the newline inside parentheses is not a statement end");
        CheckOneError("script P\n{\n    fn F() ->\n}\n",
            DiagnosticCode::ExpectedType, 3, 14, "an arrow without a return type");
        CheckOneError(InFunction("        x = a.\n"),
            DiagnosticCode::ExpectedName, 5, 15, "a dot without a member name");
        CheckOneError(InFunction("        x =\n"),
            DiagnosticCode::ExpectedExpression, 5, 12, "an assignment without a value");
        CheckOneError(InFunction("        else\n        {\n        }\n"),
            DiagnosticCode::ExpectedStatement, 5, 9, "else without if");
        CheckOneError(InFunction("        switch (s)\n        {\n            x = 1\n        }\n"),
            DiagnosticCode::ExpectedCaseOrDefault, 7, 13, "a statement directly inside switch");
        CheckOneError("enum E\n{\n    Idle Dead\n}\n",
            DiagnosticCode::ExpectedEndOfStatement, 3, 10, "enum members are one per line (jbroscript-syntax 7.2)");
        CheckOneError("script P\n{\n    + 1\n}\n",
            DiagnosticCode::ExpectedMember, 3, 5, "garbage in a type body");
        CheckOneError("script P\n{\n    Int x\n",
            DiagnosticCode::ExpectedToken, 4, 1, "a type body not closed before the end of the file");

        {
            ParseResult result("Int stray = 1\nscript A\n{\n    Int x\n}\n");
            Check(result.Diagnostics.GetCount() == 1 && DiagnosticCode::ExpectedDeclaration == result.Diagnostics.GetItems()[0].Code,
                "a field outside any type is one error");
            Check(DumpSyntaxTree(result.Tree).Std() == "CompilationUnit\n  ScriptDeclaration A\n    FieldDeclaration x\n      TypeName Int\n",
                "the declaration after the stray line still parses");
        }
        {
            // 한 문장의 에러는 하나만 낸다. 다음 문장의 에러는 다시 낸다.
            ParseResult result(InFunction("        Foo(1,, 2))\n        Bar(\n        ok = 1\n"));
            if (result.Diagnostics.GetCount() != 2)
            {
                PrintDiagnostics(result);
            }
            Check(result.Diagnostics.GetCount() == 2, "one error per broken statement, not one per token");
        }
    }

    // 문법 문서의 전체 예시(jbroscript-syntax §13)가 에러 없이 읽힌다. 문서가 바뀌면 이 테스트가 먼저 안다.
    void TestTheSyntaxDocumentExampleParses()
    {
        const std::filesystem::path path("../../tasks/jbroscript-syntax.md");
        std::ifstream file(path, std::ios::binary);
        if (false == file.is_open())
        {
            std::cout << "  [skip] no tasks/jbroscript-syntax.md two levels above the test" << std::endl;
            return;
        }
        const std::string document((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const std::size_t section = document.find("## 13.");
        Check(std::string::npos != section, "the syntax document has a section 13");
        const std::size_t fence = document.find("```\n", section);
        Check(std::string::npos != fence, "section 13 has a code block");
        const std::size_t begin = fence + 4;
        const std::size_t end = document.find("\n```", begin);
        Check(std::string::npos != end, "the code block closes");

        // 예시는 몸통을 줄인 자리에 `...` 를 쓴다. 문법이 아니므로 그 줄만 뺀다.
        std::string example;
        std::size_t lineBegin = begin;
        while (lineBegin <= end)
        {
            std::size_t lineEnd = document.find('\n', lineBegin);
            if (std::string::npos == lineEnd || lineEnd > end)
            {
                lineEnd = end;
            }
            const std::string line = document.substr(lineBegin, lineEnd - lineBegin);
            if (line.find_first_not_of(" \r") != std::string::npos && line.substr(line.find_first_not_of(' ')) != "..." && line.substr(line.find_first_not_of(' ')) != "...\r")
            {
                example += line;
            }
            example += '\n';
            lineBegin = lineEnd + 1;
        }

        ParseResult result(example);
        if (result.Diagnostics.GetCount() != 0)
        {
            PrintDiagnostics(result);
        }
        Check(result.Diagnostics.GetCount() == 0, "the full example of the syntax document parses without errors");
        const ArrayView<const NodeIndex> declarations = result.Tree.GetChildren(result.Tree.GetRoot());
        Check(declarations.Size() == 5, "the example declares an enum, an interface, a struct, a class and a script");
        Check(SyntaxKind::ScriptDeclaration == result.Tree.Get(declarations[4]).Kind
            && result.Tree.Get(declarations[4]).Text == "Enemy", "the last declaration is the Enemy script");
    }
}

int RunScriptCompilerParserTests()
{
    TestEmptyFile();
    TestTypeDeclarationsAndFields();
    TestFunctions();
    TestDeclarationsAreToldFromExpressionsByShape();
    TestOperatorPrecedence();
    TestControlFlow();
    TestRangesCoverTheirSource();
    TestErrors();
    TestTheSyntaxDocumentExampleParses();
    std::cout << "Script compiler parser tests passed.\n";
    return 0;
}
