#pragma once

#include <JBro/Editor/EditorGuideFocus.h>

#include <imgui.h>

namespace JBro::Widget
{
    // 가이드 포커스를 그리고, 위젯이 경로의 대상을 그릴 때 그 자리를 모델에 알린다(D-251).
    //
    // 모델(`EditorGuideFocus`)은 에디터가 들고, 프레임 첫머리에 `SetGuideFocus` 로 건다 - ImGui 의 현재
    // 컨텍스트와 같은 모양이다. 걸린 것이 없으면 표식은 아무 일도 하지 않는다.

    void SetGuideFocus(EditorGuideFocus* focus);
    EditorGuideFocus* GetGuideFocus();

    // **다음에 그리는 위젯에 표식을 단다.** `ImGui::SetNextItemOpen` 과 같은 결이다 - 표식을 받는 래퍼
    // (`Button`·`MenuItem`·`BeginMenu`·`FoldNode`·`CollapsingSection`·`BeginTab`·`Tree`/`TreeBegin`·`FilterCombo`)가
    // 꺼내 쓴다. 래퍼는 열어야 하는 마디면 열고, 그린 뒤 사각형과 열림을 알린다.
    // 표식을 받지 않는 위젯 앞에서 부르면 그 뒤의 첫 래퍼가 가져간다 - 표식은 받는 래퍼 바로 앞에서 부른다.
    void SetNextItemTarget(const GuideFocusTarget& target);

    // 래퍼가 쓰는 것이다. 패널이 직접 부르지 않는다.
    namespace Internal
    {
        // 달린 표식을 꺼낸다. 없으면 비었다.
        GuideFocusTarget TakeNextItemTarget();
        // 이 대상을 이번 프레임에 열어야 하면 `SetNextItemOpen(true)` 를 건다.
        void OpenIfGuided(const GuideFocusTarget& target);
        // 방금 그린 항목(`GetItemRect`)을 알린다. 지금 칸인데 잘려 있으면 그 자리로 굴린다.
        void ReportLastItem(const GuideFocusTarget& target, bool opened, bool activated);
    }

    // 항목 하나가 아닌 자리(표의 한 줄·패널 창)를 알린다. 대상이 비었으면 아무 일도 없다.
    void ReportGuideTarget(const GuideFocusTarget& target, const ImVec2& min, const ImVec2& max,
        bool opened, bool activated);

    // 말풍선에 적을 것이다. 글자는 이미 번역된 것이다(§11.2).
    struct GuideFocusBalloon
    {
        const char* title = nullptr;
        const char* body = nullptr;
        // "2 / 3" 같은 걸음 표시. 비우면 적지 않는다.
        const char* progress = nullptr;
        // 본문 아래의 한 줄(마지막 단계를 해냈을 때의 "확인을 누르면 마칩니다"). 비우면 적지 않는다.
        const char* note = nullptr;
        // 단추의 글자다. 비운 단추는 두지 않는다 - 어느 단추를 둘지는 가이드를 쓴 사람이 단계마다 정한다(`GuideStep`).
        // 차례는 건너뛰기 · 이전 · 다음이다.
        const char* skipLabel = nullptr;
        const char* backLabel = nullptr;
        const char* nextLabel = nullptr;
        // 이전 단추를 두었지만 지금은 못 누를 때(첫 단계) 회색으로 두고 까닭을 띄운다(§11.1).
        bool backEnabled = true;
        const char* backDisabledReason = nullptr;
        // 다음도 같다(오브젝트를 고르지 않았다).
        bool nextEnabled = true;
        const char* nextDisabledReason = nullptr;
    };

    // **막과 구멍과 말풍선을 그린다.** 모든 창을 그린 뒤, 알림보다도 뒤에 부른다.
    //
    // 막은 foreground draw list 가 아니라 **입력을 받지 않는 창**에 그린다. foreground 는 모든 창 위에 그려져
    // 말풍선의 단추까지 덮는다. 막 창과 말풍선 창을 차례로 맨 앞에 세운다. 대상이 연 팝업은 막 창 아래에
    // 있지만 그 자리를 구멍으로 뚫으므로 가려지지 않는다.
    //
    // 이 경로가 켜진 뒤에 열린 팝업의 자리와 말풍선의 자리를 모델에 알린다. 막이 사라졌으면 아무것도 그리지 않는다.
    GuideFocusAction GuideFocus(EditorGuideFocus& focus, const GuideFocusBalloon& balloon);
}
