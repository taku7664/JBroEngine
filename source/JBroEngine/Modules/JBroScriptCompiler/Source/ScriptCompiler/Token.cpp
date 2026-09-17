#include <JBro/ScriptCompiler/Token.h>

namespace JBro::ScriptCompiler
{
    namespace
    {
        struct KeywordEntry
        {
            std::string_view Text;
            TokenKind Kind;
        };

        // jbroscript-syntax §2.1 의 예약어 목록이다. 여기에 없는 말은 이름이다.
        constexpr KeywordEntry Keywords[] = {
            { "script", TokenKind::KeywordScript },
            { "class", TokenKind::KeywordClass },
            { "struct", TokenKind::KeywordStruct },
            { "interface", TokenKind::KeywordInterface },
            { "enum", TokenKind::KeywordEnum },
            { "fn", TokenKind::KeywordFn },
            { "public", TokenKind::KeywordPublic },
            { "protected", TokenKind::KeywordProtected },
            { "private", TokenKind::KeywordPrivate },
            { "static", TokenKind::KeywordStatic },
            { "const", TokenKind::KeywordConst },
            { "ref", TokenKind::KeywordRef },
            { "if", TokenKind::KeywordIf },
            { "else", TokenKind::KeywordElse },
            { "for", TokenKind::KeywordFor },
            { "while", TokenKind::KeywordWhile },
            { "switch", TokenKind::KeywordSwitch },
            { "case", TokenKind::KeywordCase },
            { "default", TokenKind::KeywordDefault },
            { "break", TokenKind::KeywordBreak },
            { "continue", TokenKind::KeywordContinue },
            { "return", TokenKind::KeywordReturn },
            { "and", TokenKind::KeywordAnd },
            { "or", TokenKind::KeywordOr },
            { "not", TokenKind::KeywordNot },
            { "is", TokenKind::KeywordIs },
            { "null", TokenKind::KeywordNull },
            { "true", TokenKind::KeywordTrue },
            { "false", TokenKind::KeywordFalse },
        };
    }

    TokenKind FindKeyword(std::string_view text) noexcept
    {
        for (const KeywordEntry& entry : Keywords)
        {
            if (entry.Text == text)
            {
                return entry.Kind;
            }
        }
        return TokenKind::Identifier;
    }

    const char* GetTokenKindName(TokenKind kind) noexcept
    {
        switch (kind)
        {
        case TokenKind::EndOfFile: return "EndOfFile";
        case TokenKind::Newline: return "Newline";
        case TokenKind::Identifier: return "Identifier";
        case TokenKind::IntegerLiteral: return "IntegerLiteral";
        case TokenKind::FloatLiteral: return "FloatLiteral";
        case TokenKind::StringLiteral: return "StringLiteral";
        case TokenKind::KeywordScript: return "KeywordScript";
        case TokenKind::KeywordClass: return "KeywordClass";
        case TokenKind::KeywordStruct: return "KeywordStruct";
        case TokenKind::KeywordInterface: return "KeywordInterface";
        case TokenKind::KeywordEnum: return "KeywordEnum";
        case TokenKind::KeywordFn: return "KeywordFn";
        case TokenKind::KeywordPublic: return "KeywordPublic";
        case TokenKind::KeywordProtected: return "KeywordProtected";
        case TokenKind::KeywordPrivate: return "KeywordPrivate";
        case TokenKind::KeywordStatic: return "KeywordStatic";
        case TokenKind::KeywordConst: return "KeywordConst";
        case TokenKind::KeywordRef: return "KeywordRef";
        case TokenKind::KeywordIf: return "KeywordIf";
        case TokenKind::KeywordElse: return "KeywordElse";
        case TokenKind::KeywordFor: return "KeywordFor";
        case TokenKind::KeywordWhile: return "KeywordWhile";
        case TokenKind::KeywordSwitch: return "KeywordSwitch";
        case TokenKind::KeywordCase: return "KeywordCase";
        case TokenKind::KeywordDefault: return "KeywordDefault";
        case TokenKind::KeywordBreak: return "KeywordBreak";
        case TokenKind::KeywordContinue: return "KeywordContinue";
        case TokenKind::KeywordReturn: return "KeywordReturn";
        case TokenKind::KeywordAnd: return "KeywordAnd";
        case TokenKind::KeywordOr: return "KeywordOr";
        case TokenKind::KeywordNot: return "KeywordNot";
        case TokenKind::KeywordIs: return "KeywordIs";
        case TokenKind::KeywordNull: return "KeywordNull";
        case TokenKind::KeywordTrue: return "KeywordTrue";
        case TokenKind::KeywordFalse: return "KeywordFalse";
        case TokenKind::LeftParen: return "LeftParen";
        case TokenKind::RightParen: return "RightParen";
        case TokenKind::LeftBrace: return "LeftBrace";
        case TokenKind::RightBrace: return "RightBrace";
        case TokenKind::LeftBracket: return "LeftBracket";
        case TokenKind::RightBracket: return "RightBracket";
        case TokenKind::Comma: return "Comma";
        case TokenKind::Colon: return "Colon";
        case TokenKind::Dot: return "Dot";
        case TokenKind::DotDot: return "DotDot";
        case TokenKind::Arrow: return "Arrow";
        case TokenKind::Plus: return "Plus";
        case TokenKind::Minus: return "Minus";
        case TokenKind::Star: return "Star";
        case TokenKind::Slash: return "Slash";
        case TokenKind::Percent: return "Percent";
        case TokenKind::Equal: return "Equal";
        case TokenKind::PlusEqual: return "PlusEqual";
        case TokenKind::MinusEqual: return "MinusEqual";
        case TokenKind::StarEqual: return "StarEqual";
        case TokenKind::SlashEqual: return "SlashEqual";
        case TokenKind::EqualEqual: return "EqualEqual";
        case TokenKind::BangEqual: return "BangEqual";
        case TokenKind::Less: return "Less";
        case TokenKind::LessEqual: return "LessEqual";
        case TokenKind::Greater: return "Greater";
        case TokenKind::GreaterEqual: return "GreaterEqual";
        case TokenKind::Count: break;
        }
        return "Unknown";
    }
}
