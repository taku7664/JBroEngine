#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>
#include <JBro/Types/Table.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class IPlatform;

    // 화면에 나오는 글자를 키로 다룬다(ProjectRule §11.2).
    //
    // **글자를 소스에 박으면 옮길 수가 없다.** 기존 엔진은 키 669개를 한곳에 모아
    // 두고 `ko-KR` / `en-US` 두 벌을 YAML 로 읽었다. 같은 모양으로 옮긴다.
    //
    // 키가 없을 때 무엇이 나올지는 **부르는 쪽이 정한다**. `Text` 는 키를 그대로
    // 돌려주고(번역이 빠진 자리가 화면에서 바로 보인다), `TextOr` 는 넘긴 원문을
    // 돌려준다. 코드에 있는 것은 영어 원문이므로 `TextOr` 가 기본 쓰임새다.
    class LocalizationTable final
    {
    public:
        static LocalizationTable& Get();

        // 로케일 파일을 읽는다. `<directory>/<locale>.yaml` 이다.
        //
        // **폴백도 같이 읽는다.** 현재 로케일에 없는 키가 폴백에는 있을 수 있고,
        // 그때 키를 그대로 내보내는 것보다 다른 언어로라도 보여 주는 편이 낫다.
        // 파일은 플랫폼이 연다(D-112).
        Bool Load(IPlatform& platform, const char* directory, const char* locale, const char* fallback);
        void Clear();

        // 키를 찾는다. 현재 로케일 → 폴백 → nullptr 순이다.
        const char* Find(const char* key) const;
        // 한 벌에서만 찾는다. 없으면 nullptr 이다. `Loc::TextFor` 가 원문을 사이에 끼우려고 쓴다.
        const char* FindInLocale(const char* key) const;
        const char* FindInFallback(const char* key) const;

        const String& GetLocale() const;
        const String& GetFallbackLocale() const;
        std::size_t GetCount() const;
        // 읽을 때마다 오른다. 글꼴이나 배치를 다시 재야 하는 쪽이 본다.
        UInt64 GetRevision() const;

    private:
        Bool LoadFile(IPlatform& platform, const char* directory, const char* locale,
            Table<String, String>& out) const;

        Table<String, String> m_entries;
        Table<String, String> m_fallbackEntries;
        String m_locale;
        String m_fallbackLocale;
        UInt64 m_revision = 0;
    };

    namespace Loc
    {
        // 없으면 키를 그대로 돌려준다 - 빠진 자리가 화면에서 보이게.
        const char* Text(const char* key);
        // 없으면 넘긴 원문을 돌려준다.
        const char* TextOr(const char* key, const char* fallback);
        // **원문이 어느 로케일인지 아는 글자**다(D-267). 데이터로 온 글자(가이드)는 원문이 영어라는 보장이 없다 -
        // 에디터 안의 에이전트는 사용자의 말로 적는다. 차례는 현재 로케일의 표 → 원문이 현재 로케일이면 원문 →
        // 폴백 로케일의 표 → 원문이다. 원문이 현재 로케일인데 폴백 표의 영어를 보이면 사용자는 제 말로 적힌 것을 못 본다.
        // `locale` 이 비었으면 영어 원문으로 보고 `TextOr` 와 같다.
        const char* TextFor(const char* key, const char* string, const char* locale);
    }
}
