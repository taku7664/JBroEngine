#include <JBro/ScriptCompiler/Lexer.h>

#include <cstdint>
#include <limits>

namespace JBro::ScriptCompiler
{
    namespace
    {
        bool IsLetter(char c) noexcept
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || '_' == c;
        }

        bool IsDigit(char c) noexcept
        {
            return c >= '0' && c <= '9';
        }

        bool IsLetterOrDigit(char c) noexcept
        {
            return IsLetter(c) || IsDigit(c);
        }

        class LexerState final
        {
        public:
            LexerState(const SourceText& source, DiagnosticList& diagnostics)
                : m_text(source.GetText())
                , m_diagnostics(diagnostics)
            {
            }

            Array<Token> Run()
            {
                SkipByteOrderMark();
                while (false == AtEnd())
                {
                    LexOne();
                }
                const SourceLocation end = Here();
                Token& token = m_tokens.Emplace();
                token.Kind = TokenKind::EndOfFile;
                token.Range = SourceRange{ end, end };
                token.Text = std::string_view();
                return std::move(m_tokens);
            }

        private:
            bool AtEnd() const noexcept
            {
                return m_offset >= m_text.size();
            }

            char Peek(std::size_t ahead = 0) const noexcept
            {
                const std::size_t index = m_offset + ahead;
                return index < m_text.size() ? m_text[index] : '\0';
            }

            SourceLocation Here() const noexcept
            {
                SourceLocation location;
                location.Offset = static_cast<std::uint32_t>(m_offset);
                location.Line = m_line;
                location.Column = static_cast<std::uint32_t>(m_offset - m_lineStart + 1);
                return location;
            }

            // 줄바꿈이 아닌 글자 하나를 넘긴다.
            void Advance(std::size_t count = 1) noexcept
            {
                m_offset += count;
            }

            void SkipByteOrderMark() noexcept
            {
                if (m_text.size() >= 3
                    && static_cast<unsigned char>(m_text[0]) == 0xEF
                    && static_cast<unsigned char>(m_text[1]) == 0xBB
                    && static_cast<unsigned char>(m_text[2]) == 0xBF)
                {
                    m_offset = 3;
                    m_lineStart = 3;
                }
            }

            Token& Emit(TokenKind kind, const SourceLocation& begin)
            {
                Token& token = m_tokens.Emplace();
                token.Kind = kind;
                token.Range = SourceRange{ begin, Here() };
                token.Text = m_text.substr(begin.Offset, m_offset - begin.Offset);
                return token;
            }

            SourceRange RangeFrom(const SourceLocation& begin) const noexcept
            {
                return SourceRange{ begin, Here() };
            }

            void LexOne()
            {
                const char c = Peek();
                if (' ' == c || '\t' == c || '\r' == c)
                {
                    Advance();
                    return;
                }
                if ('\n' == c)
                {
                    LexNewline();
                    return;
                }
                if ('/' == c && '/' == Peek(1))
                {
                    SkipComment();
                    return;
                }
                if (IsLetter(c))
                {
                    LexWord();
                    return;
                }
                if (IsDigit(c))
                {
                    LexNumber();
                    return;
                }
                if ('"' == c)
                {
                    LexString();
                    return;
                }
                LexPunctuation();
            }

            void LexNewline()
            {
                const SourceLocation begin = Here();
                ++m_offset;
                // 괄호 안의 줄바꿈은 문장을 끝내지 않는다(D-104). 이미 문장 끝이 나와 있거나
                // 아직 아무 토큰도 없으면 하나 더 내지 않는다.
                const bool insideBrackets = m_bracketDepth > 0;
                const bool afterNewline = m_tokens.IsEmpty() || TokenKind::Newline == m_tokens.Last().Kind;
                if (false == insideBrackets && false == afterNewline)
                {
                    Token& token = m_tokens.Emplace();
                    token.Kind = TokenKind::Newline;
                    token.Range = SourceRange{ begin, begin };
                    token.Range.End.Offset = begin.Offset + 1;
                    token.Range.End.Column = begin.Column + 1;
                    token.Text = m_text.substr(begin.Offset, 1);
                }
                ++m_line;
                m_lineStart = m_offset;
            }

            void SkipComment() noexcept
            {
                while (false == AtEnd() && '\n' != Peek())
                {
                    Advance();
                }
            }

            void LexWord()
            {
                const SourceLocation begin = Here();
                while (IsLetterOrDigit(Peek()))
                {
                    Advance();
                }
                const std::string_view text = m_text.substr(begin.Offset, m_offset - begin.Offset);
                Emit(FindKeyword(text), begin);
            }

            void LexNumber()
            {
                const SourceLocation begin = Here();
                while (IsDigit(Peek()))
                {
                    Advance();
                }
                TokenKind kind = TokenKind::IntegerLiteral;
                // `0.5` 는 실수이고 `0..n` 은 정수와 범위다. 점 뒤에 숫자가 와야 실수다.
                if ('.' == Peek() && IsDigit(Peek(1)))
                {
                    kind = TokenKind::FloatLiteral;
                    Advance();
                    while (IsDigit(Peek()))
                    {
                        Advance();
                    }
                }
                // 숫자에 글자가 바로 붙으면(`10f`, `2D`) 하나의 잘못된 숫자로 읽고 에러를 낸다.
                // 둘로 자르면 `10` 과 `f` 가 따로 나와 에러가 엉뚱한 자리를 가리킨다.
                if (IsLetter(Peek()))
                {
                    while (IsLetterOrDigit(Peek()))
                    {
                        Advance();
                    }
                    m_diagnostics.Add(DiagnosticCode::InvalidNumber, RangeFrom(begin),
                        m_text.substr(begin.Offset, m_offset - begin.Offset));
                    Emit(kind, begin);
                    return;
                }
                if (TokenKind::IntegerLiteral == kind && false == FitsInInt64(begin.Offset, m_offset))
                {
                    m_diagnostics.Add(DiagnosticCode::IntegerTooLarge, RangeFrom(begin),
                        m_text.substr(begin.Offset, m_offset - begin.Offset));
                }
                Emit(kind, begin);
            }

            bool FitsInInt64(std::size_t first, std::size_t last) const noexcept
            {
                const std::uint64_t limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
                std::uint64_t value = 0;
                for (std::size_t index = first; index < last; ++index)
                {
                    const std::uint64_t digit = static_cast<std::uint64_t>(m_text[index] - '0');
                    if (value > (limit - digit) / 10)
                    {
                        return false;
                    }
                    value = value * 10 + digit;
                }
                return true;
            }

            void LexString()
            {
                const SourceLocation begin = Here();
                Advance(); // 여는 따옴표
                while (true)
                {
                    if (AtEnd() || '\n' == Peek())
                    {
                        // 줄바꿈은 먹지 않는다. 다음 토큰이 문장 끝으로 쓴다.
                        m_diagnostics.Add(DiagnosticCode::UnterminatedString, RangeFrom(begin));
                        Emit(TokenKind::StringLiteral, begin);
                        return;
                    }
                    const char c = Peek();
                    if ('"' == c)
                    {
                        Advance();
                        Emit(TokenKind::StringLiteral, begin);
                        return;
                    }
                    if ('\\' == c)
                    {
                        const SourceLocation escapeBegin = Here();
                        const char next = Peek(1);
                        if ('"' == next || '\\' == next || 'n' == next || 't' == next)
                        {
                            Advance(2);
                            continue;
                        }
                        if ('\n' == next || '\0' == next)
                        {
                            // `"abc\` 로 줄이 끝났다. 닫히지 않은 문자열로 한 번만 알린다.
                            Advance();
                            continue;
                        }
                        Advance(2);
                        m_diagnostics.Add(DiagnosticCode::InvalidEscape, RangeFrom(escapeBegin),
                            m_text.substr(escapeBegin.Offset, 2));
                        continue;
                    }
                    Advance();
                }
            }

            void LexPunctuation()
            {
                const SourceLocation begin = Here();
                const char c = Peek();
                const char next = Peek(1);
                switch (c)
                {
                case '(':
                    Advance();
                    ++m_bracketDepth;
                    Emit(TokenKind::LeftParen, begin);
                    return;
                case ')':
                    Advance();
                    CloseBracket();
                    Emit(TokenKind::RightParen, begin);
                    return;
                case '[':
                    Advance();
                    ++m_bracketDepth;
                    Emit(TokenKind::LeftBracket, begin);
                    return;
                case ']':
                    Advance();
                    CloseBracket();
                    Emit(TokenKind::RightBracket, begin);
                    return;
                case '{':
                    Advance();
                    // 괄호 안에는 중괄호가 오지 않는다. 닫지 않은 괄호가 뒤따르는 줄바꿈을 모두 먹지 않게 한다.
                    m_bracketDepth = 0;
                    Emit(TokenKind::LeftBrace, begin);
                    return;
                case '}':
                    Advance();
                    m_bracketDepth = 0;
                    Emit(TokenKind::RightBrace, begin);
                    return;
                case ',':
                    Advance();
                    Emit(TokenKind::Comma, begin);
                    return;
                case ':':
                    Advance();
                    Emit(TokenKind::Colon, begin);
                    return;
                case '.':
                    if ('.' == next)
                    {
                        Advance(2);
                        Emit(TokenKind::DotDot, begin);
                        return;
                    }
                    Advance();
                    Emit(TokenKind::Dot, begin);
                    return;
                case '+':
                    EmitWithOptionalEqual(TokenKind::Plus, TokenKind::PlusEqual, begin);
                    return;
                case '-':
                    if ('>' == next)
                    {
                        Advance(2);
                        Emit(TokenKind::Arrow, begin);
                        return;
                    }
                    EmitWithOptionalEqual(TokenKind::Minus, TokenKind::MinusEqual, begin);
                    return;
                case '*':
                    EmitWithOptionalEqual(TokenKind::Star, TokenKind::StarEqual, begin);
                    return;
                case '/':
                    EmitWithOptionalEqual(TokenKind::Slash, TokenKind::SlashEqual, begin);
                    return;
                case '%':
                    Advance();
                    Emit(TokenKind::Percent, begin);
                    return;
                case '=':
                    EmitWithOptionalEqual(TokenKind::Equal, TokenKind::EqualEqual, begin);
                    return;
                case '<':
                    EmitWithOptionalEqual(TokenKind::Less, TokenKind::LessEqual, begin);
                    return;
                case '>':
                    EmitWithOptionalEqual(TokenKind::Greater, TokenKind::GreaterEqual, begin);
                    return;
                case '!':
                    if ('=' == next)
                    {
                        Advance(2);
                        Emit(TokenKind::BangEqual, begin);
                        return;
                    }
                    Advance();
                    m_diagnostics.Add(DiagnosticCode::UseWordOperator, RangeFrom(begin), "!", "not");
                    return;
                case '&':
                    if ('&' == next)
                    {
                        Advance(2);
                        m_diagnostics.Add(DiagnosticCode::UseWordOperator, RangeFrom(begin), "&&", "and");
                        return;
                    }
                    break;
                case '|':
                    if ('|' == next)
                    {
                        Advance(2);
                        m_diagnostics.Add(DiagnosticCode::UseWordOperator, RangeFrom(begin), "||", "or");
                        return;
                    }
                    break;
                default:
                    break;
                }
                LexUnexpected(begin);
            }

            void EmitWithOptionalEqual(TokenKind single, TokenKind withEqual, const SourceLocation& begin)
            {
                if ('=' == Peek(1))
                {
                    Advance(2);
                    Emit(withEqual, begin);
                    return;
                }
                Advance();
                Emit(single, begin);
            }

            void CloseBracket() noexcept
            {
                if (m_bracketDepth > 0)
                {
                    --m_bracketDepth;
                }
            }

            // 모르는 글자다. UTF-8 한 글자를 통째로 넘겨 에러가 글자 중간을 가리키지 않게 한다.
            void LexUnexpected(const SourceLocation& begin)
            {
                const unsigned char lead = static_cast<unsigned char>(Peek());
                std::size_t length = 1;
                if (lead >= 0xF0)
                {
                    length = 4;
                }
                else if (lead >= 0xE0)
                {
                    length = 3;
                }
                else if (lead >= 0xC0)
                {
                    length = 2;
                }
                if (m_offset + length > m_text.size())
                {
                    length = m_text.size() - m_offset;
                }
                Advance(length);
                m_diagnostics.Add(DiagnosticCode::UnexpectedCharacter, RangeFrom(begin),
                    m_text.substr(begin.Offset, length));
            }

            std::string_view m_text;
            DiagnosticList& m_diagnostics;
            Array<Token> m_tokens;
            std::size_t m_offset = 0;
            std::size_t m_lineStart = 0;
            std::uint32_t m_line = 1;
            std::uint32_t m_bracketDepth = 0;
        };
    }

    Array<Token> Lex(const SourceText& source, DiagnosticList& diagnostics)
    {
        LexerState state(source, diagnostics);
        return state.Run();
    }

    String DecodeStringLiteral(std::string_view literal)
    {
        String result;
        if (literal.size() < 1 || '"' != literal.front())
        {
            return result;
        }
        std::size_t end = literal.size();
        if (end >= 2 && '"' == literal.back())
        {
            end -= 1;
        }
        result.Reserve(end);
        for (std::size_t index = 1; index < end; ++index)
        {
            const char c = literal[index];
            if ('\\' != c || index + 1 >= end)
            {
                result.push_back(c);
                continue;
            }
            const char next = literal[index + 1];
            switch (next)
            {
            case 'n': result.push_back('\n'); break;
            case 't': result.push_back('\t'); break;
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            default:
                result.push_back('\\');
                result.push_back(next);
                break;
            }
            ++index;
        }
        return result;
    }
}
