#pragma once

#include <JBro/ScriptCompiler/SourceText.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <cstdint>
#include <string_view>

namespace JBro::ScriptCompiler
{
    // 컴파일러가 사용자에게 알리는 문제의 종류다.
    //
    // **메시지는 문장이 아니라 키로 들고 있다**(jbroc-rules §3.3). 글은 `Localization/jbroc/<로케일>.yaml`
    // 에 있고 `DiagnosticMessages` 가 언어를 골라 채운다. 새 코드를 더하면 `GetDiagnosticKey` 와
    // 두 로케일 파일에 함께 더한다 - 테스트가 빠진 키를 잡는다.
    enum class DiagnosticCode : std::uint16_t
    {
        // 렉서
        UnexpectedCharacter,       // {0} 글자
        UseWordOperator,           // {0} 쓴 연산자(&&), {1} 대신 쓸 말(and)
        UnterminatedString,
        InvalidEscape,             // {0} 이스케이프
        InvalidNumber,             // {0} 숫자처럼 보이는 글
        IntegerTooLarge,           // {0} 정수

        Count,
    };

    enum class DiagnosticSeverity : std::uint8_t
    {
        Error,
    };

    struct Diagnostic
    {
        DiagnosticCode Code = DiagnosticCode::UnexpectedCharacter;
        DiagnosticSeverity Severity = DiagnosticSeverity::Error;
        SourceRange Range;
        // 메시지의 {0}, {1} ... 자리에 들어갈 글이다.
        Array<String> Arguments;
    };

    // 한 번의 컴파일에서 나온 진단을 모은다. 순서는 나온 순서다.
    class DiagnosticList final
    {
    public:
        Diagnostic& Add(DiagnosticCode code, const SourceRange& range);
        Diagnostic& Add(DiagnosticCode code, const SourceRange& range, std::string_view argument);
        Diagnostic& Add(DiagnosticCode code, const SourceRange& range,
            std::string_view firstArgument, std::string_view secondArgument);

        const Array<Diagnostic>& GetItems() const noexcept { return m_items; }
        std::size_t GetCount() const noexcept { return m_items.Size(); }
        bool HasErrors() const noexcept;
        void Clear() noexcept { m_items.Clear(); }

    private:
        Array<Diagnostic> m_items;
    };

    // 로케일 파일의 키다. 예: `jbroc.lex.unterminated_string`.
    const char* GetDiagnosticKey(DiagnosticCode code) noexcept;
}
