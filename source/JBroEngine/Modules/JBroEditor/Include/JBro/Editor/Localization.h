#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>
#include <JBro/Types/Table.h>

namespace JBro
{
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
        bool Load(const char* directory, const char* locale, const char* fallback);
        void Clear();

        // 키를 찾는다. 현재 로케일 → 폴백 → nullptr 순이다.
        const char* Find(const char* key) const;

        const String& GetLocale() const;
        const String& GetFallbackLocale() const;
        std::size_t GetCount() const;
        // 읽을 때마다 오른다. 글꼴이나 배치를 다시 재야 하는 쪽이 본다.
        std::uint64_t GetRevision() const;

    private:
        bool LoadFile(const char* directory, const char* locale,
            Table<String, String>& out) const;

        Table<String, String> m_entries;
        Table<String, String> m_fallbackEntries;
        String m_locale;
        String m_fallbackLocale;
        std::uint64_t m_revision = 0;
    };

    namespace Loc
    {
        // 없으면 키를 그대로 돌려준다 - 빠진 자리가 화면에서 보이게.
        const char* Text(const char* key);
        // 없으면 넘긴 원문을 돌려준다.
        const char* TextOr(const char* key, const char* fallback);
    }
}
