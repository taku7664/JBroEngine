#include <JBro/ScriptCompiler/Diagnostic.h>

namespace JBro::ScriptCompiler
{
    Diagnostic& DiagnosticList::Add(DiagnosticCode code, const SourceRange& range)
    {
        Diagnostic& diagnostic = m_items.Emplace();
        diagnostic.Code = code;
        diagnostic.Range = range;
        return diagnostic;
    }

    Diagnostic& DiagnosticList::Add(DiagnosticCode code, const SourceRange& range, std::string_view argument)
    {
        Diagnostic& diagnostic = Add(code, range);
        diagnostic.Arguments.Add(String(argument));
        return diagnostic;
    }

    Diagnostic& DiagnosticList::Add(DiagnosticCode code, const SourceRange& range,
        std::string_view firstArgument, std::string_view secondArgument)
    {
        Diagnostic& diagnostic = Add(code, range);
        diagnostic.Arguments.Add(String(firstArgument));
        diagnostic.Arguments.Add(String(secondArgument));
        return diagnostic;
    }

    bool DiagnosticList::HasErrors() const noexcept
    {
        for (const Diagnostic& diagnostic : m_items)
        {
            if (diagnostic.Severity == DiagnosticSeverity::Error)
            {
                return true;
            }
        }
        return false;
    }

    const char* GetDiagnosticKey(DiagnosticCode code) noexcept
    {
        switch (code)
        {
        case DiagnosticCode::UnexpectedCharacter:
            return "jbroc.lex.unexpected_character";
        case DiagnosticCode::UseWordOperator:
            return "jbroc.lex.use_word_operator";
        case DiagnosticCode::UnterminatedString:
            return "jbroc.lex.unterminated_string";
        case DiagnosticCode::InvalidEscape:
            return "jbroc.lex.invalid_escape";
        case DiagnosticCode::InvalidNumber:
            return "jbroc.lex.invalid_number";
        case DiagnosticCode::IntegerTooLarge:
            return "jbroc.lex.integer_too_large";
        case DiagnosticCode::ExpectedDeclaration:
            return "jbroc.parse.expected_declaration";
        case DiagnosticCode::ExpectedMember:
            return "jbroc.parse.expected_member";
        case DiagnosticCode::ExpectedName:
            return "jbroc.parse.expected_name";
        case DiagnosticCode::ExpectedType:
            return "jbroc.parse.expected_type";
        case DiagnosticCode::ExpectedExpression:
            return "jbroc.parse.expected_expression";
        case DiagnosticCode::ExpectedStatement:
            return "jbroc.parse.expected_statement";
        case DiagnosticCode::ExpectedToken:
            return "jbroc.parse.expected_token";
        case DiagnosticCode::ExpectedEndOfStatement:
            return "jbroc.parse.expected_end_of_statement";
        case DiagnosticCode::ExpectedBlock:
            return "jbroc.parse.expected_block";
        case DiagnosticCode::MissingConditionParentheses:
            return "jbroc.parse.missing_condition_parentheses";
        case DiagnosticCode::ExpectedIn:
            return "jbroc.parse.expected_in";
        case DiagnosticCode::ExpectedNullAfterIs:
            return "jbroc.parse.expected_null_after_is";
        case DiagnosticCode::ExpectedCaseOrDefault:
            return "jbroc.parse.expected_case_or_default";
        case DiagnosticCode::MissingFieldType:
            return "jbroc.parse.missing_field_type";
        case DiagnosticCode::Count:
            break;
        }
        return "jbroc.unknown";
    }
}
