#pragma once

#include <JBro/Editor/Widget/Common.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro::Widget
{
    // 계층·디테일처럼 **나뉜 칸의 머리와 줄**에 붙는 작은 위젯들이다(모델 에디터 다시 짜기, D-465).
    // 패널마다 줄 끝에 글자를 직접 얹거나 머리 위에 단추를 손으로 놓던 것을 한곳으로 모은다.

    // **바로 앞 항목(머리·제목 줄)의 오른쪽 끝에 아이콘 단추를 얹는다.** 만들 때 앞 항목의 칸을 떠 두고,
    // `Icon` 을 부를 때마다 오른쪽부터 왼쪽으로 하나씩 놓는다. 접는 머리는 `CollapsingSection(…, allowOverlap = true)` 로 그려야
    // 단추가 마우스를 받는다. 단추는 앞 항목과 같은 줄에 서므로 아래 줄의 자리는 바뀌지 않는다.
    class TrailingButtons
    {
    public:
        TrailingButtons();

        // 누르면 참이다. `disabled` 면 흐리고 눌리지 않는다.
        Bool Icon(const char* id, const char* icon, const char* tooltip, Bool disabled = false);

    private:
        ImVec2 m_min;
        ImVec2 m_max;
        Float m_right = 0.0f;
    };

    // **바로 앞 줄의 오른쪽 끝(창 내용의 오른쪽)에 글자를 쓴다.** 트리 줄 끝의 값(마스크 가중치 등)이 그 자리다.
    // 트리 항목의 칸은 글자 폭까지라 항목 끝에 붙이면 이름과 겹친다. `muted` 면 흐리다. 자리를 차지하지 않는다.
    void RowTrailingText(const char* text, Bool muted = false);

    // **디테일 칸의 제목 줄**이다: 아이콘·이름·흐린 설명 한 줄과 아래 선. 고른 것이 무엇인지 칸 맨 위에서 말한다.
    // 줄 전체가 하나의 항목이라 뒤에 `TrailingButtons` 를 쓰면 줄 끝에 단추가 선다. `caption` 은 없어도 된다.
    void DetailHeading(const char* icon, const char* title, const char* caption = nullptr);

    // **빈 칸의 안내**다. 남은 칸의 가운데에 흐린 아이콘과 짧은 글을 둔다(고른 것이 없는 디테일 칸).
    void EmptyState(const char* icon, const char* text);
}
