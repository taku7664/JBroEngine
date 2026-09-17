#pragma once

#include <JBro/ScriptCompiler/SourceText.h>

#include <cstdint>
#include <string_view>

namespace JBro::ScriptCompiler
{
    // 토큰의 종류다.
    //
    // **예약어는 jbroscript-syntax §2.1 의 목록뿐이다.** `callback`·`override`·`require`·`in` 은
    // 함수 선언 끝이나 `for` 괄호 안에서만 뜻을 가지므로 렉서는 이름(`Identifier`)으로 내고
    // 파서가 자리를 보고 가른다. C++ 키워드(`new`·`template`)도 이름이다.
    enum class TokenKind : std::uint8_t
    {
        EndOfFile,
        // 문장 끝이다. 연속된 줄바꿈은 하나로 모으고, `( )`·`[ ]` 안에서는 내지 않는다(D-104).
        Newline,

        Identifier,
        IntegerLiteral,
        FloatLiteral,
        StringLiteral,

        // 선언
        KeywordScript,
        KeywordClass,
        KeywordStruct,
        KeywordInterface,
        KeywordEnum,
        KeywordFn,
        // 멤버
        KeywordPublic,
        KeywordProtected,
        KeywordPrivate,
        KeywordStatic,
        KeywordConst,
        KeywordRef,
        // 문장
        KeywordIf,
        KeywordElse,
        KeywordFor,
        KeywordWhile,
        KeywordSwitch,
        KeywordCase,
        KeywordDefault,
        KeywordBreak,
        KeywordContinue,
        KeywordReturn,
        // 식
        KeywordAnd,
        KeywordOr,
        KeywordNot,
        KeywordIs,
        KeywordNull,
        KeywordTrue,
        KeywordFalse,

        LeftParen,      // (
        RightParen,     // )
        LeftBrace,      // {
        RightBrace,     // }
        LeftBracket,    // [
        RightBracket,   // ]
        Comma,          // ,
        Colon,          // :
        Dot,            // .
        DotDot,         // ..
        Arrow,          // ->

        Plus,           // +
        Minus,          // -
        Star,           // *
        Slash,          // /
        Percent,        // %
        Equal,          // =
        PlusEqual,      // +=
        MinusEqual,     // -=
        StarEqual,      // *=
        SlashEqual,     // /=
        EqualEqual,     // ==
        BangEqual,      // !=
        Less,           // <
        LessEqual,      // <=
        Greater,        // >
        GreaterEqual,   // >=

        Count,
    };

    struct Token
    {
        TokenKind Kind = TokenKind::EndOfFile;
        SourceRange Range;
        // 원문의 그 글자들이다. 문자열 리터럴은 따옴표와 이스케이프를 포함한 그대로다.
        // `SourceText` 를 가리키므로 그것보다 오래 쓰지 않는다.
        std::string_view Text;
    };

    // 테스트와 진단에 쓰는 이름이다. 예: `KeywordRef`, `Arrow`.
    const char* GetTokenKindName(TokenKind kind) noexcept;

    // 예약어면 그 종류를, 아니면 `Identifier` 를 돌려준다.
    TokenKind FindKeyword(std::string_view text) noexcept;

    bool IsKeyword(TokenKind kind) noexcept;
}
