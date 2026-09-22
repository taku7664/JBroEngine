#pragma once

namespace JBro::Icons
{
    // 에디터가 쓰는 Font Awesome 글리프다(D-96). UTF-8 로 적어 두어 글자와 이어 붙일 수 있다.
    // 아이콘 글꼴이 없는 기계에서는 네모로 나온다 - 그때도 에디터는 뜬다.
    //
    // 기존 엔진 `EditorIcons` 의 이름을 따르되, 실제로 쓰는 것만 둔다. 쓰지 않는 글리프를
    // 미리 나열하면 어느 것이 화면에 있는지 알 수 없다.
    inline constexpr const char* GripLines = "\xEF\x9E\xA4"; // f7a4 - 목록 행의 손잡이
    inline constexpr const char* Xmark = "\xEF\x80\x8D";     // f00d - 지우기·삭제 표시
    inline constexpr const char* Plus = "\x2B";                // '+' - 아이콘 글꼴에 기대지 않는다
    inline constexpr const char* Gear = "\xEF\x80\x93";      // f013
    inline constexpr const char* Eye = "\xEF\x81\xAE";       // f06e
    inline constexpr const char* EyeSlash = "\xEF\x81\xB0";  // f070
    inline constexpr const char* Filter = "\xEF\x82\xB0";    // f0b0
    inline constexpr const char* Search = "\xEF\x80\x82";    // f002
    inline constexpr const char* FolderOpen = "\xEF\x81\xBC"; // f07c

    // 아이콘 글꼴이 덮는 유니코드 범위다. 글꼴을 합칠 때 이 범위만 아이콘 글꼴에서 가져온다.
    inline constexpr unsigned short RangeBegin = 0xF000;
    inline constexpr unsigned short RangeEnd = 0xF8FF;
}
