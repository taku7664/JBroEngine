// jbroc 의 렉서를 찌른다(D-104).
//
// 토큰 열은 종류 이름을 공백으로 이은 한 줄 글로 바꿔 비교한다. 틀렸을 때 기대와 실제를 나란히 보이기 위해서다.
// 문법 문서(jbroscript-syntax.md)가 정한 규칙마다, 그 규칙이 틀렸을 때 달라지는 입력을 하나씩 둔다.

#include <JBro/ScriptCompiler/DiagnosticMessages.h>
#include <JBro/ScriptCompiler/Lexer.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
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

    struct LexResult
    {
        explicit LexResult(const char* text)
            : Source(String("test.jscript"), String(text))
        {
            Tokens = Lex(Source, Diagnostics);
        }

        SourceText Source;
        DiagnosticList Diagnostics;
        Array<Token> Tokens;
    };

    std::string KindsOf(const Array<Token>& tokens)
    {
        std::string result;
        for (const Token& token : tokens)
        {
            if (false == result.empty())
            {
                result += ' ';
            }
            result += GetTokenKindName(token.Kind);
        }
        return result;
    }

    void CheckKinds(const char* text, const char* expected, const char* message)
    {
        LexResult result(text);
        const std::string actual = KindsOf(result.Tokens);
        if (actual != expected)
        {
            std::cout << "  source:   " << text << '\n'
                << "  expected: " << expected << '\n'
                << "  actual:   " << actual << '\n';
        }
        Check(actual == expected, message);
    }

    void CheckOnlyDiagnostic(const LexResult& result, DiagnosticCode code, const char* message)
    {
        if (result.Diagnostics.GetCount() != 1 || result.Diagnostics.GetItems()[0].Code != code)
        {
            std::cout << "  diagnostics: " << result.Diagnostics.GetCount() << '\n';
            for (const Diagnostic& diagnostic : result.Diagnostics.GetItems())
            {
                std::cout << "    " << GetDiagnosticKey(diagnostic.Code) << '\n';
            }
        }
        Check(result.Diagnostics.GetCount() == 1 && result.Diagnostics.GetItems()[0].Code == code, message);
    }

    void TestReservedWordsAreKeywordsAndContextualWordsAreNames()
    {
        CheckKinds("script class struct interface enum fn",
            "KeywordScript KeywordClass KeywordStruct KeywordInterface KeywordEnum KeywordFn EndOfFile",
            "declaration words are keywords");
        CheckKinds("public protected private static const ref",
            "KeywordPublic KeywordProtected KeywordPrivate KeywordStatic KeywordConst KeywordRef EndOfFile",
            "member words are keywords");
        CheckKinds("if else for while switch case default break continue return",
            "KeywordIf KeywordElse KeywordFor KeywordWhile KeywordSwitch KeywordCase KeywordDefault KeywordBreak KeywordContinue KeywordReturn EndOfFile",
            "statement words are keywords");
        CheckKinds("and or not is null true false",
            "KeywordAnd KeywordOr KeywordNot KeywordIs KeywordNull KeywordTrue KeywordFalse EndOfFile",
            "expression words are keywords");
        // 함수 선언 끝과 for 괄호 안에서만 뜻을 갖는 말, 그리고 C++ 키워드는 이름이다(syntax §2.1).
        CheckKinds("callback override require in template new delete int float bool let var",
            "Identifier Identifier Identifier Identifier Identifier Identifier Identifier Identifier Identifier Identifier Identifier Identifier EndOfFile",
            "contextual words, C++ keywords and old syntax words are names");
        // 예약어가 이름의 일부이면 이름이다.
        CheckKinds("refCount isDead fnPtr notes",
            "Identifier Identifier Identifier Identifier EndOfFile",
            "a keyword inside a longer name does not split it");
    }

    void TestNewlinesEndStatementsOutsideBrackets()
    {
        CheckKinds("a\nb", "Identifier Newline Identifier EndOfFile",
            "a newline ends a statement");
        CheckKinds("a\n\n\n   \nb", "Identifier Newline Identifier EndOfFile",
            "blank lines collapse into one statement end");
        CheckKinds("\n\n// leading comment\na", "Identifier EndOfFile",
            "newlines before the first token are not statement ends");
        CheckKinds("a // trailing comment\nb", "Identifier Newline Identifier EndOfFile",
            "a comment does not swallow the newline after it");
        CheckKinds("a\r\nb", "Identifier Newline Identifier EndOfFile",
            "CRLF is one newline");
        CheckKinds("Raycast(from,\n    down,\n    1.0)",
            "Identifier LeftParen Identifier Comma Identifier Comma FloatLiteral RightParen EndOfFile",
            "newlines inside parentheses do not end the statement");
        CheckKinds("lines[\n0\n]\nx",
            "Identifier LeftBracket IntegerLiteral RightBracket Newline Identifier EndOfFile",
            "newlines inside brackets do not end the statement, and the one after the bracket does");
        CheckKinds("f((a)\n)\ny",
            "Identifier LeftParen LeftParen Identifier RightParen RightParen Newline Identifier EndOfFile",
            "nested parentheses are counted, not just flagged");
        // 닫지 않은 괄호가 뒤따르는 모든 줄바꿈을 먹으면 파서가 파일 끝까지 한 문장으로 읽는다.
        CheckKinds("if (a\n{\nb\n}",
            "KeywordIf LeftParen Identifier LeftBrace Newline Identifier Newline RightBrace EndOfFile",
            "a brace resets an unclosed parenthesis so later lines still end statements");
        CheckKinds(")\na", "RightParen Newline Identifier EndOfFile",
            "a stray closing parenthesis does not make the depth negative");
    }

    void TestNumbers()
    {
        CheckKinds("20 0.5 10.0", "IntegerLiteral FloatLiteral FloatLiteral EndOfFile",
            "integers and floats");
        CheckKinds("0..count", "IntegerLiteral DotDot Identifier EndOfFile",
            "0..count is an integer and a range, not a float");
        CheckKinds("a.b", "Identifier Dot Identifier EndOfFile", "member access");
        CheckKinds("table[i].Weight", "Identifier LeftBracket Identifier RightBracket Dot Identifier EndOfFile",
            "member access after indexing");

        {
            LexResult result("10f");
            CheckOnlyDiagnostic(result, DiagnosticCode::InvalidNumber, "a letter glued to a number is an error");
            Check(KindsOf(result.Tokens) == "IntegerLiteral EndOfFile",
                "the bad number stays one token so the error points at the whole of it");
            Check(result.Diagnostics.GetItems()[0].Arguments[0] == "10f", "the error names the whole bad number");
        }
        {
            LexResult result("1.5x");
            CheckOnlyDiagnostic(result, DiagnosticCode::InvalidNumber, "a letter glued to a float is an error");
        }
        {
            LexResult result("9223372036854775807");
            Check(result.Diagnostics.GetCount() == 0, "the largest Int fits");
        }
        {
            LexResult result("9223372036854775808");
            CheckOnlyDiagnostic(result, DiagnosticCode::IntegerTooLarge, "one past the largest Int does not fit");
        }
    }

    void TestStrings()
    {
        {
            LexResult result("\"낙하 간격\"");
            Check(result.Diagnostics.GetCount() == 0, "a Korean string is fine");
            Check(KindsOf(result.Tokens) == "StringLiteral EndOfFile", "a string is one token");
            Check(DecodeStringLiteral(result.Tokens[0].Text) == "낙하 간격", "the string decodes to its text");
        }
        {
            LexResult result("\"a\\\"b\\\\c\\nd\\te\"");
            Check(result.Diagnostics.GetCount() == 0, "the four escapes are accepted");
            Check(DecodeStringLiteral(result.Tokens[0].Text) == "a\"b\\c\nd\te", "the four escapes decode");
        }
        {
            LexResult result("\"a\\qb\"");
            CheckOnlyDiagnostic(result, DiagnosticCode::InvalidEscape, "an unknown escape is an error");
            Check(result.Diagnostics.GetItems()[0].Arguments[0] == "\\q", "the error names the escape");
        }
        {
            LexResult result("\"open\nnext");
            CheckOnlyDiagnostic(result, DiagnosticCode::UnterminatedString, "a string must close before the line ends");
            Check(KindsOf(result.Tokens) == "StringLiteral Newline Identifier EndOfFile",
                "the newline after an unclosed string still ends the statement");
        }
        {
            LexResult result("\"open\\");
            CheckOnlyDiagnostic(result, DiagnosticCode::UnterminatedString,
                "a backslash at the end of the file is one unclosed string, not also a bad escape");
        }
        CheckKinds("\"// not a comment\" x", "StringLiteral Identifier EndOfFile",
            "// inside a string is not a comment");
    }

    void TestOperatorsAndPunctuation()
    {
        CheckKinds("( ) { } [ ] , : . .. ->",
            "LeftParen RightParen LeftBrace RightBrace LeftBracket RightBracket Comma Colon Dot DotDot Arrow EndOfFile",
            "punctuation");
        CheckKinds("+ - * / % = += -= *= /=",
            "Plus Minus Star Slash Percent Equal PlusEqual MinusEqual StarEqual SlashEqual EndOfFile",
            "arithmetic and assignment");
        CheckKinds("== != < <= > >=",
            "EqualEqual BangEqual Less LessEqual Greater GreaterEqual EndOfFile",
            "comparison");
        CheckKinds("a-b", "Identifier Minus Identifier EndOfFile", "minus without spaces");
        CheckKinds("fn F() -> Int", "KeywordFn Identifier LeftParen RightParen Arrow Identifier EndOfFile",
            "the return arrow");
        CheckKinds("Array<Table<String, Int>>",
            "Identifier Less Identifier Less Identifier Comma Identifier Greater Greater EndOfFile",
            ">> is two closing angle brackets because there is no shift operator");
    }

    void TestCOperatorsPointToTheWords()
    {
        {
            LexResult result("a && b");
            CheckOnlyDiagnostic(result, DiagnosticCode::UseWordOperator, "&& asks for and");
            Check(result.Diagnostics.GetItems()[0].Arguments[1] == "and", "&& suggests and");
        }
        {
            LexResult result("a || b");
            CheckOnlyDiagnostic(result, DiagnosticCode::UseWordOperator, "|| asks for or");
            Check(result.Diagnostics.GetItems()[0].Arguments[1] == "or", "|| suggests or");
        }
        {
            LexResult result("!a");
            CheckOnlyDiagnostic(result, DiagnosticCode::UseWordOperator, "! asks for not");
            Check(result.Diagnostics.GetItems()[0].Arguments[1] == "not", "! suggests not");
        }
        {
            LexResult result("a & b");
            CheckOnlyDiagnostic(result, DiagnosticCode::UnexpectedCharacter, "a single & is just an unexpected character");
        }
    }

    void TestUnexpectedCharactersAndNames()
    {
        {
            LexResult result("점수 = 1");
            Check(result.Diagnostics.GetCount() == 2, "each Korean letter outside a string is one error, not one per byte");
            Check(result.Diagnostics.GetItems()[0].Arguments[0] == "점", "the error names the whole letter");
            Check(KindsOf(result.Tokens) == "Equal IntegerLiteral EndOfFile",
                "lexing continues after an unexpected character");
        }
        {
            LexResult result("a @ b # c");
            Check(result.Diagnostics.GetCount() == 2, "every unexpected character is reported, not only the first");
        }
    }

    void TestLocations()
    {
        LexResult result("script Enemy\n\tInt hp = 10\r\n  hp += 1");
        const Array<Token>& tokens = result.Tokens;
        // script Enemy \n Int hp = 10 \n hp += 1 EOF
        Check(tokens[0].Range.Begin.Line == 1 && tokens[0].Range.Begin.Column == 1, "first token starts at 1:1");
        Check(tokens[1].Range.Begin.Column == 8 && tokens[1].Range.End.Column == 13, "a name spans its columns");
        Check(tokens[3].Text == "Int" && tokens[3].Range.Begin.Line == 2 && tokens[3].Range.Begin.Column == 2,
            "a tab counts as one column and the line advances");
        Check(tokens[8].Text == "hp" && tokens[8].Range.Begin.Line == 3 && tokens[8].Range.Begin.Column == 3,
            "CRLF advances one line, not two");
        Check(tokens[9].Text == "+=" && tokens[9].Range.Begin.Offset == 32, "offsets count bytes from the start");

        LexResult withBom("\xEF\xBB\xBFscript A");
        Check(withBom.Diagnostics.GetCount() == 0, "a UTF-8 byte order mark is not an unexpected character");
        Check(withBom.Tokens[0].Kind == TokenKind::KeywordScript && withBom.Tokens[0].Range.Begin.Column == 1,
            "the byte order mark does not shift the first column");
    }

    void TestTheWholeExampleLexesClean()
    {
        // jbroscript-syntax §13 의 전체 예시와 같은 문법을 모두 지나가는 글이다. 에러가 없어야 한다.
        LexResult result(
            "script Enemy : IDamageable\n"
            "{\n"
            "    [range(1, 100), category(\"Stats\")]\n"
            "    Int MaxHp = 10\n"
            "    ref Transform2D target\n"
            "    Array<ref Enemy> allies\n"
            "    fn TakeDamage(Int amount) override\n"
            "    {\n"
            "        hp -= amount\n"
            "        if (hp <= 0 and state != EnemyState.Dead)\n"
            "        {\n"
            "            state = EnemyState.Dead\n"
            "        }\n"
            "    }\n"
            "    static fn SumWeights(ref const Array<DropEntry> table) -> Int\n"
            "    {\n"
            "        for (i in 0..table.Size())\n"
            "        {\n"
            "            total += table[i].Weight\n"
            "        }\n"
            "        return Float(hp) / Float(MaxHp)\n"
            "    }\n"
            "}\n");
        Check(result.Diagnostics.GetCount() == 0, "the example has no lexical errors");
        Check(result.Tokens.Last().Kind == TokenKind::EndOfFile, "the token list ends with EndOfFile");
    }

    // 로케일 파일 두 벌이 모든 진단 키를 채우고, 두 언어의 글이 서로 다른지 본다(jbroc-rules §8).
    // 한쪽만 보면 번역이 아예 안 되어도 통과한다.
    void TestEveryDiagnosticHasBothTranslations()
    {
        DiagnosticMessages korean;
        DiagnosticMessages english;
        if (false == korean.Load("Localization/jbroc", "ko-KR", nullptr))
        {
            std::cout << "  [skip] no Localization/jbroc directory beside the test" << std::endl;
            return;
        }
        Check(english.Load("Localization/jbroc", "en-US", nullptr), "the English table loads when the Korean one does");

        const auto count = static_cast<std::uint16_t>(DiagnosticCode::Count);
        for (std::uint16_t index = 0; index < count; ++index)
        {
            const char* key = GetDiagnosticKey(static_cast<DiagnosticCode>(index));
            const char* ko = korean.Find(key);
            const char* en = english.Find(key);
            if (nullptr == ko || nullptr == en)
            {
                std::cout << "  missing translation: " << key << '\n';
            }
            Check(nullptr != ko && nullptr != en, "every diagnostic key is in both tables");
            Check(0 != std::strcmp(ko, en), "the Korean and English messages differ");
        }
        Check(korean.GetCount() == count && english.GetCount() == count,
            "the tables have no keys that no diagnostic uses");
    }

    std::filesystem::path MakeProbeDirectory()
    {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() / "JBroScriptCompilerMessagesProbe";
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        return directory;
    }

    void WriteFile(const std::filesystem::path& path, const char* text)
    {
        std::ofstream file(path, std::ios::binary);
        file << text;
    }

    void TestMessagesFillArgumentsAndFallBack()
    {
        const std::filesystem::path directory = MakeProbeDirectory();
        WriteFile(directory / "ko-KR.yaml",
            "Locale: ko-KR\n"
            "Entries:\n"
            "  jbroc.lex.use_word_operator: \"{0} 대신 {1}\"\n");
        WriteFile(directory / "en-US.yaml",
            "Locale: en-US\n"
            "Entries:\n"
            "  jbroc.lex.use_word_operator: \"Use {1} instead of {0}.\"\n"
            "  jbroc.lex.invalid_number: \"Not a number: {0}\"\n");

        DiagnosticMessages messages;
        Check(messages.Load(directory.string().c_str(), "ko-KR", "en-US"), "the probe tables load");

        Diagnostic both;
        both.Code = DiagnosticCode::UseWordOperator;
        both.Arguments.Add(String("&&"));
        both.Arguments.Add(String("and"));
        Check(messages.Format(both) == "&& 대신 and", "arguments fill their places in order");

        Diagnostic fallback;
        fallback.Code = DiagnosticCode::InvalidNumber;
        fallback.Arguments.Add(String("10f"));
        Check(messages.Format(fallback) == "Not a number: 10f", "a key missing in the locale comes from the fallback");

        Diagnostic missing;
        missing.Code = DiagnosticCode::IntegerTooLarge;
        Check(messages.Format(missing) == "jbroc.lex.integer_too_large",
            "a key missing everywhere shows the key, so the gap is visible");

        Diagnostic oneArgument;
        oneArgument.Code = DiagnosticCode::UseWordOperator;
        oneArgument.Arguments.Add(String("&&"));
        Check(messages.Format(oneArgument) == "&& 대신 {1}", "a missing argument leaves its place visible");

        DiagnosticMessages untouched;
        Check(untouched.Load(directory.string().c_str(), "ko-KR", nullptr), "load once");
        Check(false == untouched.Load(directory.string().c_str(), "fr-FR", nullptr), "an absent locale fails");
        Check(untouched.GetLocale() == "ko-KR" && untouched.GetCount() == 1,
            "a failed load keeps the table that was already there");

        std::filesystem::remove_all(directory);
    }
}

int RunScriptCompilerLexerTests()
{
    TestReservedWordsAreKeywordsAndContextualWordsAreNames();
    TestNewlinesEndStatementsOutsideBrackets();
    TestNumbers();
    TestStrings();
    TestOperatorsAndPunctuation();
    TestCOperatorsPointToTheWords();
    TestUnexpectedCharactersAndNames();
    TestLocations();
    TestTheWholeExampleLexesClean();
    TestEveryDiagnosticHasBothTranslations();
    TestMessagesFillArgumentsAndFallBack();
    std::cout << "Script compiler lexer tests passed.\n";
    return 0;
}
