#pragma once

#include <JBro/Editor/Widget/Common.h>

#include <JBro/Types/ArrayView.h>

namespace JBro::Widget
{
    // 검색 칸이 달린 목록 드롭다운이다. 기존 엔진의 `ImFilterCombo` 를 옮겼다.
    //
    // **목록을 고르는 자리는 전부 이것을 쓴다**(D-116). enum 칸(`EnumCombo`),
    // 에셋 칸(`AssetField`), 인스펙터의 컴포넌트 추가가 같은 몸이다 - 검색·빈 글·
    // 키보드 동작을 한 곳에서만 고친다.
    //
    // 항목은 이름 배열의 뷰다. 위젯은 이름을 복사하지 않으므로 부르는 쪽이 `Draw` 동안
    // 살려 둔다. 현재 번호가 범위 밖(보통 -1)이면 트리거에 `EmptyText` 가 보인다 -
    // "아직 고르지 않음" 과 "고르면 실행하는 단추"(컴포넌트 추가) 둘 다 이 모양이다.
    //
    // 팝업이 열리면 검색 칸에 포커스가 가고, 검색 글은 열 때마다 비운다. 검색 칸에서
    // Enter 를 누르면 **보이는 첫 항목**을 고른다 - 이름 몇 자 치고 Enter 가 가장
    // 빠른 길이다. 화살표 이동은 ImGui 의 키보드 탐색이 한다.
    //
    // 돌려주는 값: 참이면 현재 번호가 바뀌었다(같은 항목을 다시 골라도 거짓이다).
    class FilterCombo
    {
    public:
        FilterCombo(const char* id, ArrayView<const char* const> items, int& currentIndex);

        // 현재 번호가 범위 밖일 때 트리거에 보일 글.
        FilterCombo& EmptyText(const char* text);
        // 검색 칸의 힌트. 기본은 로컬라이징된 "검색".
        FilterCombo& FilterHint(const char* text);
        // 항목이 하나도 없을 때 팝업 안에 보일 글. 기본은 로컬라이징된 "일치하는 항목이 없습니다".
        FilterCombo& NoItemsText(const char* text);
        // 거짓이면 검색 칸을 그리지 않는다. 항목이 몇 개뿐인 enum 에는 검색이 소음이다.
        FilterCombo& ShowFilter(bool show = true);
        FilterCombo& Width(float width);
        // 스크롤 없이 보이는 최대 줄 수. 1~8 로 자른다.
        FilterCombo& MaxVisibleItems(int count);

        bool Draw() const;
        bool operator()() const;

        static constexpr int DefaultMaxVisibleItems = 8;

    private:
        const char* m_id = nullptr;
        ArrayView<const char* const> m_items;
        int& m_currentIndex;
        const char* m_emptyText = nullptr;
        const char* m_filterHint = nullptr;
        const char* m_noItemsText = nullptr;
        float m_width = 0.0f;
        int m_maxVisibleItems = DefaultMaxVisibleItems;
        bool m_showFilter = true;
    };

    // 대소문자를 가리지 않는 부분 일치다. 빈 필터는 모두에 맞는다. 위젯 밖에서도
    // (에셋 브라우저의 이름 걸러내기) 같은 규칙을 써야 하므로 공개한다.
    bool MatchesFilter(const char* text, const char* filter);
}
