#pragma once

namespace JBro::Icons
{
    // 에디터가 쓰는 Material Design Icons 글리프다(D-277). UTF-8 로 적어 두어 글자와 이어 붙일 수 있다.
    // 아이콘 글꼴이 없는 기계에서는 네모로 나온다 - 그때도 에디터는 뜬다.
    //
    // 글리프는 U+F0000 위(보충 사용자 영역)에 있어 UTF-8 로 4 바이트다. ImGui 가 이것을 읽으려면
    // `IMGUI_USE_WCHAR32` 가 켜져 있어야 한다(`imconfig.h`) - 꺼지면 모두 네모가 된다.
    //
    // 이름은 기존 엔진 `EditorIcons` 의 것을 그대로 두고, 주석에 MDI 의 이름(`mdi-...`)을 적는다.
    // 실제로 쓰는 것만 둔다. 쓰지 않는 글리프를 미리 나열하면 어느 것이 화면에 있는지 알 수 없다.
    inline constexpr const char* GripLines = "\xF3\xB1\x8B\xB0";  // F12F0 drag-horizontal-variant - 목록 행의 손잡이
    inline constexpr const char* Xmark = "\xF3\xB0\x85\x96";      // F0156 close - 지우기·삭제 표시
    inline constexpr const char* Plus = "\x2B";                    // '+' - 아이콘 글꼴에 기대지 않는다
    inline constexpr const char* Gear = "\xF3\xB0\x92\x93";       // F0493 cog
    inline constexpr const char* Eye = "\xF3\xB0\x88\x88";        // F0208 eye
    inline constexpr const char* EyeSlash = "\xF3\xB0\x88\x89";   // F0209 eye-off
    inline constexpr const char* Filter = "\xF3\xB0\x88\xB2";     // F0232 filter
    inline constexpr const char* Search = "\xF3\xB0\x8D\x89";     // F0349 magnify
    inline constexpr const char* FolderOpen = "\xF3\xB0\x9D\xB0"; // F0770 folder-open

    // 아이콘 글꼴이 덮는 유니코드 범위다. 글꼴을 합칠 때 이 범위만 아이콘 글꼴에서 가져온다.
    // MDI 7.4.47 의 글리프는 F0001..F1D17 에 있다(자리 표시 `blank` 하나만 F68C).
    inline constexpr unsigned int RangeBegin = 0xF0000;
    inline constexpr unsigned int RangeEnd = 0xF1FFF;
}
