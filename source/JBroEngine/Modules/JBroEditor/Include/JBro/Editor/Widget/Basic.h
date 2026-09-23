#pragma once

#include <JBro/Editor/Widget/Common.h>
#include <JBro/RHI/RHI.h>

#include <imgui.h>

namespace JBro::Widget
{
    // 패널이 가장 자주 쓰는 원시 위젯들이다(D-152).
    //
    // 기존 엔진은 `ImText`·`ImTextButton`·`ImPopup` 을 거쳐 글자와 단추와 메뉴를 그렸다
    // (`Application/Editor/ImItem/`). 우리 패널은 이 셋을 `ImGui::` 로 곧장 불러,
    // 같은 "흐린 안내 글자" 가 패널마다 `TextDisabled("%s", …)`·`TextDisabled(…)` 로 갈리고
    // 메뉴 항목의 잠금 처리도 저마다 달랐다(§11.1). 모양을 한곳에서 정해 두면 바꿀 때도 한곳이다.

    // ── 글자 ────────────────────────────────────────────────────────────────
    // 그대로 적는다. `%` 가 들어 있어도 서식으로 읽지 않는다.
    void Text(const char* text);
    // 서식을 채워 적는다.
    void TextF(const char* format, ...) IM_FMTARGS(1);
    // 칸 끝에서 줄을 바꾸는 글자다. 경로처럼 길이를 모르는 값이 잘리지 않게 한다(D-160).
    void WrappedText(const char* text);
    // **흐린 안내 글자**다. "선택한 것이 없습니다" 처럼 값이 아니라 상태를 말하는 자리에 쓴다.
    void HintText(const char* text);
    void HintTextF(const char* format, ...) IM_FMTARGS(1);
    // 심각도 색으로 적는다. 경고·오류가 본문과 같은 색이면 눈에 걸리지 않는다.
    void SeverityTextF(Severity severity, const char* format, ...) IM_FMTARGS(2);

    // ── 단추 ────────────────────────────────────────────────────────────────
    // 글자 단추다. 눌렸으면 참이다.
    bool Button(const char* label);
    // 보이지 않는 누름 자리다. 그림·칸 위를 누르게 할 때 쓴다. 받을 단추를 고를 수 있다 -
    // 캔버스 뷰는 가운데 단추로 끌어 옮긴다.
    bool HitArea(const char* id, const ImVec2& size,
        ImGuiButtonFlags buttons = ImGuiButtonFlags_MouseButtonLeft);

    // ── 메뉴 ────────────────────────────────────────────────────────────────
    // 누르면 참이다. `enabled` 가 거짓이면 **회색으로 보이고 눌리지 않는다** - 숨기면
    // 그 기능이 있다는 것조차 알 수 없고, 눌리는데 아무 일도 없으면 고장인지 모른다.
    //
    // `disabledReason` 을 주면 회색일 때 마우스를 올린 자리에 그 까닭이 뜬다(D-181,
    // 기존 엔진의 저장 항목이 그랬다). **회색으로만 두면 무엇을 해야 켜지는지 알 수 없다.**
    bool MenuItem(const char* label, const char* shortcut = nullptr, bool enabled = true,
        const char* disabledReason = nullptr);

    // 방금 그린 항목이 회색일 때 그 까닭을 띄운다. 메뉴 항목이 아닌 것(단추·칸)도
    // 같은 수를 쓸 수 있도록 따로 낸다. `disabled` 가 거짓이거나 까닭이 없으면 아무 일도 없다.
    void DisabledReason(bool disabled, const char* reason);
    // 켜고 끄는 항목이다. 바뀌었으면 참이고 `checked` 가 새 값이다.
    bool MenuToggle(const char* label, bool& checked, bool enabled = true);

    // 우클릭 메뉴다. 열렸으면 참이고, 그때만 `EndContextMenu` 를 부른다.
    // `ofWindow` 가 참이면 항목이 아니라 창의 빈 곳에 붙는다(항목 위에서는 열리지 않는다).
    bool BeginContextMenu(const char* id, bool ofWindow = false);
    void EndContextMenu();

    // **부르는 쪽이 여는 우클릭 메뉴**다(D-170). 창 메뉴(`ofWindow`)는 그 자리에 위젯이 있으면
    // 열지 않는데(`NoOpenOverItems`), 캔버스 뷰에서는 기즈모 손잡이가 늘 고른 것 위에 있어
    // **오브젝트의 한가운데를 우클릭하면 메뉴가 열리지 않았다**. 열 때를 부르는 쪽이 정하고
    // (끌지 않은 오른쪽 버튼), 여기서는 열려 있을 때만 그린다.
    void OpenContextMenu(const char* id);
    bool BeginOpenedContextMenu(const char* id);
    // 묻는 창이다. `OpenModal` 로 열고, `BeginModal` 이 참일 때만 `EndModal` 을 부른다.
    void OpenModal(const char* id);
    bool BeginModal(const char* id);
    void CloseModal();
    void EndModal();

    // ── 칸 ──────────────────────────────────────────────────────────────────
    // **필드 표 안에서 접는 마디**다. `Widget::Tree` 는 고름·올려놓음 배경을 줄 왼쪽 끝부터
    // 칠해, 목록 행에 놓으면 손잡이와 번호를 덮는다 - 인스펙터의 중첩 필드는 이것을 쓴다.
    bool FoldNode(const char* label, ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None);
    // 트리 마디를 닫는다. `Tree`·`TreeBegin` 이 참을 돌려줬을 때만 부른다.
    void TreePop();
    // 접는 머리다. 펼쳐져 있으면 참이다.
    bool CollapsingSection(const char* title, bool defaultOpen = true);
    // RHI 텍스처를 붙인다. `uvMin`·`uvMax` 로 텍스처의 일부(시트의 한 칸)만 보일 수 있다.
    void Image(TextureHandle texture, const ImVec2& size,
        const ImVec2& uvMin = ImVec2(0.0f, 0.0f), const ImVec2& uvMax = ImVec2(1.0f, 1.0f));
    // `width`×`height` 그림을 `box` 안에 비율을 지켜 가장 크게 넣은 크기다(D-159). 크기를 모르면(0) 칸 그대로다.
    // 그림을 칸에 늘여 붙이면 픽셀 아트가 찌그러진다 - 미리보기·아이콘·뷰어가 모두 이것을 쓴다.
    ImVec2 FitInside(std::uint32_t width, std::uint32_t height, const ImVec2& box);

    // ── 탭 ──────────────────────────────────────────────────────────────────
    // 탭 줄이다. `BeginTabs` 가 참일 때만 `EndTabs` 를 부른다.
    bool BeginTabs(const char* id);
    void EndTabs();
    // 탭 하나다. `open` 을 주면 닫기 단추가 서고, 눌리면 거짓이 된다. 앞에 있으면 참이고
    // 그때만 `EndTab` 을 부른다. `select` 가 참이면 이번 프레임에 앞으로 꺼낸다.
    bool BeginTab(const char* label, bool* open = nullptr, bool select = false);
    void EndTab();
}
