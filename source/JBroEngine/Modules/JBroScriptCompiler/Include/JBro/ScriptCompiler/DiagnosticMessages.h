#pragma once

#include <JBro/ScriptCompiler/Diagnostic.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/String.h>
#include <JBro/Types/Table.h>

#include <string_view>

namespace JBro::ScriptCompiler
{
    // 진단 메시지의 번역 표다(D-104).
    //
    // `<directory>/<locale>.yaml` 을 읽는다. 모양은 에디터의 로케일 파일과 같다.
    //
    //     Locale: ko-KR
    //     Entries:
    //       jbroc.lex.unterminated_string: 문자열이 닫히지 않았습니다.
    //
    // 현재 로케일에 없는 키는 폴백 로케일에서 찾고, 그래도 없으면 **키를 그대로** 낸다.
    // 번역이 빠진 자리가 메시지에서 바로 보이게 하기 위해서다.
    class DiagnosticMessages final
    {
    public:
        // 폴백을 같이 읽는다. 현재 로케일 파일을 읽지 못하면 false 다.
        // 폴백 파일이 없는 것은 실패로 치지 않는다. `directory` 는 UTF-8 경로다.
        bool Load(const char* directory, const char* locale, const char* fallbackLocale);

        // 현재 로케일 → 폴백 → nullptr 순이다.
        const char* Find(const char* key) const;

        // 메시지를 채운다. 글 안의 {0}, {1} 을 진단의 인자로 바꾼다.
        String Format(const Diagnostic& diagnostic) const;

        // 진단이 아닌 메시지(명령줄의 알림)를 키로 채운다. 규칙은 `Format` 과 같다.
        String FormatKey(const char* key, ArrayView<const String> arguments) const;

        const String& GetLocale() const noexcept { return m_locale; }
        std::size_t GetCount() const noexcept { return m_entries.Size(); }

    private:
        Table<String, String> m_entries;
        Table<String, String> m_fallbackEntries;
        String m_locale;
    };

    // `JBroc` 이 한 진단을 내는 한 줄이다(D-105). 끝에 줄바꿈은 없다.
    //
    //     Enemy.jscript(5,12): error JBC2010: if의 조건은 괄호로 감싸야 합니다.
    //
    // MSVC 의 모양이라 VS Code 의 `$msCompile` 문제 매처가 그대로 읽는다(ide-plan §6 P0).
    String FormatDiagnosticLine(std::string_view path, const Diagnostic& diagnostic, const DiagnosticMessages& messages);
}
