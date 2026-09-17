#pragma once

#include <JBro/ScriptCompiler/Diagnostic.h>
#include <JBro/Types/String.h>
#include <JBro/Types/Table.h>

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
        // 폴백 파일이 없는 것은 실패로 치지 않는다.
        bool Load(const char* directory, const char* locale, const char* fallbackLocale);

        // 현재 로케일 → 폴백 → nullptr 순이다.
        const char* Find(const char* key) const;

        // 메시지를 채운다. 글 안의 {0}, {1} 을 진단의 인자로 바꾼다.
        String Format(const Diagnostic& diagnostic) const;

        const String& GetLocale() const noexcept { return m_locale; }
        std::size_t GetCount() const noexcept { return m_entries.Size(); }

    private:
        Table<String, String> m_entries;
        Table<String, String> m_fallbackEntries;
        String m_locale;
    };
}
