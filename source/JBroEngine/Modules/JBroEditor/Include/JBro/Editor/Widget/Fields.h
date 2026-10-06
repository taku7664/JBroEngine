#pragma once

#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/DragDrop.h>

#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::Widget
{
    // 찾기 칸이다. 글자가 있으면 오른쪽에 지우개가 붙는다.
    //
    // 그냥 `InputTextWithHint` 와 다른 점은 지우개다. 긴 목록을 좁혀 놓고 다시
    // 전체를 보려면 글자를 지워야 하는데, 지우려고 칸을 비우는 동작은 한 번에
    // 안 된다 - 누를 것이 있어야 한다.
    class SearchBox
    {
    public:
        SearchBox(const char* id, String& text);

        SearchBox& Hint(const char* text);
        SearchBox& Width(Float width);
        SearchBox& ShowClear(Bool show = true);
        SearchBox& ClearTooltip(const char* text);
        SearchBox& Flags(ImGuiInputTextFlags flags);

        Bool Draw() const;
        Bool operator()() const;

    private:
        const char* m_id = nullptr;
        String& m_text;
        const char* m_hint = nullptr;
        Float m_width = 0.0f;
        Bool m_showClear = true;
        const char* m_clearTooltip = nullptr;
        ImGuiInputTextFlags m_flags = ImGuiInputTextFlags_None;
    };

    // **한 줄이 한 이름인 목록**이다(D-189, 기존 `ImNameListEdit`). 무시 패턴처럼
    // 짧은 글자가 몇 개 늘어선 값을 고친다 - 더하기는 줄을 쓰는 것이고 빼기는 줄을
    // 지우는 것이라, `+` 와 `-` 단추가 따로 필요 없다.
    //
    // **버퍼는 부르는 쪽이 든다.** 여러 줄 글자 칸은 편집하는 동안 그 버퍼가 프레임을
    // 넘어 살아 있어야 한다 - 매 프레임 목록에서 새로 지으면 커서와 선택이 풀린다.
    // 그래서 버퍼가 원본이고 목록은 거기서 나온다.
    //
    // 칸 자체는 `TextField` 의 여러 줄 형태다. 이 함수가 더하는 것은 **한 줄이 한 이름**
    // 이라는 약속과 그 약속을 지키는 `SplitLines` / `JoinLines` 뿐이다.
    //
    // 돌려주는 값: 참이면 버퍼가 바뀌었다. `SplitLines` 로 목록을 다시 만든다.
    Bool NameListEdit(const char* id, String& buffer, Float lines = 4.0f);
    // 버퍼를 줄 단위로 가른다. 빈 줄은 버린다 - 사람이 엔터를 한 번 더 친 것이
    // 이름 없는 항목이 되면 안 된다.
    void SplitLines(const String& buffer, Array<String>& out);
    // 거꾸로. 목록을 버퍼에 담는다. 창을 열 때 한 번 부른다.
    void JoinLines(const Array<String>& items, String& buffer);

    // 상태를 한눈에 보여 주는 작은 표다. 무게에 따라 테두리와 바탕이 물든다.
    class StatusBadge
    {
    public:
        explicit StatusBadge(const char* text);

        StatusBadge& Level(Severity severity);
        StatusBadge& Tooltip(const char* text);
        StatusBadge& MinWidth(Float width);

        void Draw() const;
        void operator()() const;

    private:
        const char* m_text = nullptr;
        const char* m_tooltip = nullptr;
        Severity m_severity = Severity::Info;
        Float m_minWidth = 0.0f;
    };

    // 아이콘 하나짜리 단추다(도구 막대의 단추). `JBro::Icons` 의 글리프를 넘긴다(D-96, D-278).
    //
    // - 크기를 주지 않으면 줄 높이의 정사각형이다. 글리프는 잉크로 한가운데 놓는다(`DrawGlyphCentered`, D-277).
    // - **이름은 툴팁이 말한다.** 글자가 없는 단추라 툴팁이 곧 이름이다 - 비워 두지 않는다.
    // - **켜고 끄는 단추는 `Selected` 로 지금 상태를 칠한다.** 아이콘만 있으면 글자가 바뀌는 것으로도 상태를 알 수 없다.
    // - `Caption` 은 아이콘 뒤에 붙는 짧은 글자다. 아이콘 하나로 두 상태가 갈리지 않는 자리(단위 `유닛`/`픽셀`)에 쓴다.
    // - `id` 는 ImGui 의 이름 그대로다(`##canvas_grid`). 번역이 바뀌어도 시험·가이드가 같은 Id 로 찾는다.
    // - 가이드 포커스에는 켜져 있는가(`Selected`)를 열림으로 알린다.
    class IconButton
    {
    public:
        IconButton(const char* id, const char* icon);

        IconButton& Tooltip(const char* text);
        IconButton& Size(ImVec2 size);
        IconButton& Selected(Bool selected = true);
        IconButton& Disabled(Bool disabled = true);
        IconButton& Caption(const char* text);

        Bool Draw() const;
        Bool operator()() const;

    private:
        const char* m_id = nullptr;
        const char* m_icon = nullptr;
        const char* m_tooltip = nullptr;
        const char* m_caption = nullptr;
        ImVec2 m_size = ImVec2(0.0f, 0.0f);
        Bool m_selected = false;
        Bool m_disabled = false;
    };

    // 켜기 칸. 인스펙터의 bool 잎사귀와 컴포넌트 `사용` 칸이 이것이다(§11.1).
    Bool Checkbox(const char* id, Bool& value);

    // 레이어 비트 묶음을 이름으로 고르는 칸이다(D-233). `names` 는 비트마다 하나(32 개)이고 빈 이름의 비트는 켜져 있을 때만
    // `#번호` 로 보인다. 펼치면 "모두"·"없음" 과 레이어마다 켜기 칸이 있다. 닫힌 칸은 켜진 이름들을 잇는다.
    Bool LayerMaskField(const char* id, ArrayView<const char* const> names, UInt32& mask);

    // 오브젝트 하나를 고르는 칸이다(D-233). 검색되는 목록에서 고르거나, 하이어라키의 줄을 끌어 놓는다. `dropKind` 끌기로
    // 놓이면 그 꾸러미(8 바이트 번호)를 `dropped` 에 담고 참을 돌려준다 - 고른 번호(`chosen`)는 그대로다.
    Bool ObjectField(const char* id, ArrayView<const char* const> names, Int32& chosen, DragKind dropKind,
        UInt64& dropped);

    // 색 하나. 견본과 고르개가 붙는다 - 숫자 네 개가 아니라 색이다(§11.3).
    Bool ColorField(const char* id, Float rgba[4]);

    // 한 줄에 칸 여럿의 실수 묶음(`Vector2`·`Rect`). 칸마다 번호를 쌓으므로 첫 칸의 Id 는
    // `PushID(0)` 아래다. 범위를 주면 슬라이더, 아니면 끌기다.
    Bool ScalarRunField(const char* id, Float* values, Int32 count, Float speed,
        Bool hasRange, Float rangeMin, Float rangeMax);

    // 두 칸 사이를 끌어 나누는 손잡이다. `size` 를 직접 고쳐 준다.
    //
    // ImGui 에는 이것이 없다 - `SameLine` 과 보이지 않는 버튼으로 매번 손으로
    // 만들게 되어 있고, 그러면 화면마다 굵기와 색이 달라진다.
    Bool Splitter(
        const char* id,
        Bool vertical,
        Float thickness,
        Float* size,
        Float minSize,
        Float maxSize);
}
