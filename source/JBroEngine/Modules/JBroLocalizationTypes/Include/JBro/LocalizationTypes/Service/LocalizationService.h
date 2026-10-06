#pragma once

#include <JBro/Types/String.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::Service
{
    // 스크립트가 게임 문자열 표를 쓰는 표면이다(D-226). 표는 로케일마다 `.jstrings` 파일 하나이고, 로케일은 그 메타에 적는다.
    //
    //     localization.SetLocale("en-US");
    //     String title = localization.GetText("menu.start");   // 지금 로케일 → 폴백 로케일 → 키 그대로
    //
    // `Text2D`·`Text3D` 의 `textKey` 는 이것을 부르지 않아도 로케일을 따라 바뀐다. 이 서비스는 글자를 코드에서 조립할 때 쓴다.
    // 돌려주는 글자는 이 모듈 사본(게임 DLL)의 힙에 둔다. Main-thread only. 호스트의 구현을 소유하지 않고 바인딩된 것을 부른다.
    class LocalizationService
    {
    public:
        Bool IsReady() const;

        // 로케일을 바꾼다. 화면의 `textKey` 텍스트는 다음 그리기에 새 로케일로 바뀐다. 빈 이름은 거짓이다.
        Bool SetLocale(const char* locale) const;
        String GetLocale() const;
        // 키의 글자다. 지금 로케일에도 폴백 로케일에도 없으면 키 그대로다 - 빠진 번역이 화면에서 보인다.
        String GetText(const char* key) const;
        // 찾으면 참이고 `out` 에 글자다. 없으면 거짓이고 `out` 은 빈다.
        Bool TryGetText(const char* key, String& out) const;
        // 찾는 결과가 바뀔 때마다 오른다. 조립한 글자를 들고 있는 스크립트가 다시 만들지 이것으로 정한다. 호스트가 없으면 0 이다.
        UInt32 GetRevision() const;
    };
}
