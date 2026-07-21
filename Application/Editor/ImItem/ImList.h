#pragma once

#include <cstdio>
#include <type_traits>
#include <utility>
#include <vector>

#include "ThirdParty/imgui/imgui.h"
#include "Editor/ImGuiUtillity.h"   // ImGui::Utillity::StyleBuilder, DisableScope
#include "Editor/ImItem/ImButton.h" // ImTextButton
#include "Editor/ImItem/ImItemLocalizationKeys.h"
#include "Core/Localization/LocalizationManager.h"

enum EImListFlags
{
    IMLIST_FLAGS_NONE = 0,
    IMLIST_FLAGS_READ_ONLY = 1 << 0,
    // 추가/삭제/편집은 되되 **드래그 재정렬만** 막는다. 저장소에 순서 개념이 없는
    // 컨테이너(Table 등)용 — READ_ONLY 로는 편집까지 같이 막혀서 대신 쓸 수 없다.
    IMLIST_FLAGS_NO_REORDER = 1 << 1,
    // 핸들 옆에 [0] [1] … 행 번호를 표시한다. 순서가 의미를 갖는 목록(Array)용이다.
    // 기본으로 켜지 않는 이유: Table 처럼 순서가 없는 목록에선 번호가 거짓 정보가 되고,
    // 스키마 편집처럼 가로가 빠듯한 목록에선 콘텐츠 폭만 깎아 먹는다.
    IMLIST_FLAGS_SHOW_INDEX = 1 << 2,
};

// ── ListVirtual (코어) ───────────────────────────────────────────────
// 목록 UI/상호작용만 담당하고 **저장소는 모른다**. 원소 접근은 전부 콜백이 한다.
// std::vector 가 아닌 컨테이너(예: 반영 Array — 타입이 소거돼 void* + 함수포인터로만
// 만질 수 있다)도 이걸로 그린다.
//
//   drawRow(index)        -> bool. 그 행을 그리고, 값이 편집됐으면 true.
//   addElement()          -> 맨 뒤에 기본값 1개 추가.
//   removeElement(index)  -> 해당 원소 삭제.
//   moveElement(from, to) -> from 을 빼서 to **위치에 삽입**. to 는 이미 보정된
//                            원소 인덱스다(슬롯 인덱스가 아니다 — 보정은 코어가 한다).
//
// 반환: true 면 추가/삭제/재정렬 또는 행 편집으로 상태가 변했음.
//
// drawAddRow 를 넘기면 목록 맨 아래의 "원소 추가" 자리를 그것이 대신 그린다(반환 true = 상태 변경).
// 그 자리에 그냥 버튼 하나가 아니라 **입력 행**을 두어야 하는 경우를 위한 것이다 — 예를 들어
// Table 은 새 원소에 키가 있어야 하는데, 키는 중복될 수 있어 넣어 보기 전에 사용자가 정해야 한다.
// 콜백은 일반 행과 같은 들여쓰기·폭 안에서 호출되므로 위아래 행과 자연히 정렬된다.
// 넘기지 않으면 종전대로 "원소 추가" 항목이 그려지고 addElement 가 불린다.
struct ImListNoAddRow {};

template <typename TDrawRowFunc, typename TAddFunc, typename TRemoveFunc, typename TMoveFunc,
    typename TDrawAddRowFunc = ImListNoAddRow>
bool ImListVirtual(const char* id, int count,
    TDrawRowFunc&& drawRow, TAddFunc&& addElement, TRemoveFunc&& removeElement, TMoveFunc&& moveElement,
    EImListFlags flags = IMLIST_FLAGS_NONE,
    TDrawAddRowFunc&& drawAddRow = ImListNoAddRow{})
{
    constexpr bool hasCustomAddRow =
        false == std::is_same_v<std::decay_t<TDrawAddRowFunc>, ImListNoAddRow>;

    ImGuiStyle& style = ImGui::GetStyle();

    const bool readOnly = (0 != (flags & IMLIST_FLAGS_READ_ONLY));
    // 읽기 전용이면 당연히 재정렬도 안 되고, NO_REORDER 면 편집만 열어 둔다.
    const bool reorderable = (false == readOnly) && (0 == (flags & IMLIST_FLAGS_NO_REORDER));

    // 행 번호 칸의 폭. 자릿수가 늘어도 콘텐츠 시작이 흔들리지 않도록 **가장 긴 번호**
    // 기준으로 한 번 재서 모든 행에 같은 폭을 쓴다(행마다 재면 9→10 에서 칸이 튄다).
    const bool showIndex = (0 != (flags & IMLIST_FLAGS_SHOW_INDEX));
    float indexWidth = 0.0f;
    if (showIndex)
    {
        char widestIndex[16] = {};
        std::snprintf(widestIndex, sizeof(widestIndex), "[%d]", count > 0 ? count - 1 : 0);
        indexWidth = ImGui::CalcTextSize(widestIndex).x + style.ItemSpacing.x;
    }

    bool changed = false;
    ImGui::PushID(id);

    // 컴팩트: 행 간격 최소.
    ImGui::Utillity::StyleBuilder compact;
    compact.PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 1.0f));
    compact.PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 2.0f));

    // 박스 외곽
    const ImGuiChildFlags childFlags = ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY;
    ImGui::BeginChild("##list_body", ImVec2(0.0f, 0.0f), childFlags);

    // 좌측 드래그 핸들 글리프. 추가 행도 같은 폭만큼 들여써야 위 행들과 세로로 맞는다.
    constexpr const char* ROW_HANDLE_GLYPH = "\xEF\x83\x89";
    constexpr float ROW_HANDLE_W = 14.0f;
    constexpr float ROW_REMOVE_W = 22.0f;
    constexpr float SLOT_HEIGHT = 3.0f;
    // 드래그 페이로드 식별자 — 같은 List 인스턴스 안에서만 유효하게
    // 호출자의 id 를 함께 사용한다.
    constexpr const char* DRAG_PAYLOAD = "JBRO_LIST_REORDER";

    int removeIndex = -1;
    int moveFrom = -1;
    int moveTo = -1;

    auto drawDropSlot = [&](int slotIndex)
        {
            const ImVec2 prevSpacing = style.ItemSpacing;
            ImGui::Utillity::StyleBuilder styleBuilder;
            styleBuilder.PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(prevSpacing.x, 0.0f));
            // 사이에 얇은 invisible 영역 — drag drop target 으로 사용.
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const float  availW = ImGui::GetContentRegionAvail().x;
            ImGui::PushID(slotIndex);
            ImGui::InvisibleButton("##slot", ImVec2(availW, SLOT_HEIGHT));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(DRAG_PAYLOAD))
                {
                    moveFrom = *static_cast<const int*>(p->Data);
                    moveTo = slotIndex;
                }
                // hover 시 시각적 가이드 라인 — 2px, 슬롯 정중앙.
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(cursor.x, cursor.y + SLOT_HEIGHT * 0.5f),
                    ImVec2(cursor.x + availW, cursor.y + SLOT_HEIGHT * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        };

    for (int i = 0; i < count; ++i)
    {
        // 재정렬을 안 받으면 드롭 슬롯 자체를 두지 않는다.
        if (reorderable)
        {
            drawDropSlot(i);
        }

        ImGui::PushID(i);

        const ImVec2    availSpace = ImGui::GetContentRegionAvail();
        const float     frameHeight = ImGui::GetFrameHeight();
        const float		rowAvailW = ImGui::GetContentRegionAvail().x;
        const float		contentW = rowAvailW - ROW_HANDLE_W - ROW_REMOVE_W - indexWidth - 8.0f;
        const ImVec2    bodyStartCursor = ImGui::GetCursorPos();

        // 좌측 핸들 — 드래그 소스. 핸들만 잡아야 콘텐츠의 일반 InputText 와
        // 충돌하지 않는다. 행 높이는 콘텐츠(프레임)와 동일하게 — 컴팩트.
        const char* selectableLabel = ROW_HANDLE_GLYPH;
        ImVec2 bodySize = ImVec2(availSpace.x, frameHeight);
        ImGui::Selectable("##row_body", false, ImGuiSelectableFlags_AllowOverlap, bodySize);
        if (reorderable)
        {	// DragDrop Start
            ImGui::Utillity::StyleBuilder styleBuilder;
            styleBuilder.PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            styleBuilder.PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            styleBuilder.PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            auto dragFlags = ImGuiDragDropFlags_AcceptNoDrawDefaultRect | ImGuiDragDropFlags_SourceNoHoldToOpenOthers;
            if (ImGui::BeginDragDropSource(dragFlags))
            {
                styleBuilder.PopStyle();
                int srcIdx = i;
                ImGui::SetDragDropPayload(DRAG_PAYLOAD, &srcIdx, sizeof(int));
                {
                    ImGui::Utillity::DisableScope disable;
                    drawRow(i);
                }
                ImGui::EndDragDropSource();
            }
        }
        const ImVec2 bodyEndCursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(bodyStartCursor);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(selectableLabel);
        ImGui::SameLine();

        if (showIndex)
        {
            // ⚠ 여기서 AlignTextToFramePadding 을 **다시 부르거나** 커서 Y 를 만지면 안 된다.
            //   그 함수는 커서를 옮기는 게 아니라 이 줄의 TextBaseOffset 을 세우는 것이고,
            //   핸들을 그릴 때 이미 세워졌다. 같은 줄에서 Y 를 건드리면 그 정렬과 충돌해
            //   숫자만 아래로 밀린다(예전에 그렇게 어긋났다). X 만 조정한다.
            char indexText[16] = {};
            std::snprintf(indexText, sizeof(indexText), "[%d]", i);
            const float indexStartX = ImGui::GetCursorPosX();
            ImGui::TextDisabled("%s", indexText);
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetCursorPosX(indexStartX + indexWidth);
        }

        // 컨텐츠 영역
        ImGui::BeginGroup();
        ImGui::PushItemWidth(contentW);
        if (readOnly)
        {
            ImGui::Utillity::DisableScope disable;
            drawRow(i);
        }
        else
        {
            changed |= drawRow(i);
        }
        ImGui::PopItemWidth();
        ImGui::EndGroup();

        // 우측 삭제 버튼
        if (false == readOnly)
        {
            ImGui::SameLine();
            const char* removeLabel = "\xEF\x80\x8D";
            if (ImTextButton(removeLabel, ImVec2(0, 0), ImVec2(0, -1)))
            {
                removeIndex = i;
            }
        }
        ImGui::PopID();
        ImGui::SetCursorPos(bodyEndCursor);
    }

    if (reorderable)
    {
        // 마지막 원소 뒤의 슬롯 (맨 끝으로 이동)
        drawDropSlot(count);
    }

    if (removeIndex >= 0)
    {
        removeElement(removeIndex);
        changed = true;
    }
    else if (moveFrom >= 0 && moveTo >= 0
        && moveFrom != moveTo && moveFrom + 1 != moveTo)
    {
        // 슬롯 인덱스는 "이 원소 **앞**" 을 뜻한다. 원본을 먼저 빼내므로 뒤쪽으로
        // 옮길 때는 목표가 한 칸 당겨진다 — 보정을 여기서 끝내고 콜백에는 최종
        // 원소 인덱스만 넘긴다(콜백마다 같은 실수를 반복하지 않도록).
        int targetIndex = moveTo;
        if (moveFrom < targetIndex)
        {
            --targetIndex;
        }
        moveElement(moveFrom, targetIndex);
        changed = true;
    }

    if (false == readOnly)
    {
        if constexpr (hasCustomAddRow)
        {
            // 일반 행과 같은 자리에 오도록 핸들 폭만큼 들여쓰고, 콘텐츠 폭도 동일하게 밀어 넣는다.
            // 오른쪽 버튼 자리(ROW_REMOVE_W)를 빼 두었으므로 호출부가 SameLine 으로 그리면 맞춰진다.
            const float addAvailW   = ImGui::GetContentRegionAvail().x;
            const float addContentW = addAvailW - ROW_HANDLE_W - ROW_REMOVE_W - 8.0f;
            ImGui::Dummy(ImVec2(ImGui::CalcTextSize(ROW_HANDLE_GLYPH).x, 0.0f));
            ImGui::SameLine();
            ImGui::PushItemWidth(addContentW);
            changed |= drawAddRow();
            ImGui::PopItemWidth();
        }
        else
        {
            if (ImGui::Selectable(Loc::Text(ImItemLocKeys::ListAddElement), false, ImGuiSelectableFlags_None))
            {
                addElement();
                changed = true;
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopID();
    compact.PopStyle();
    return changed;
}

// ── List ────────────────────────────────────────────────────────────
// 마지막 하단 + / - 버튼.  좌측의 핸들(=) 을 잡아 다른 원소 사이의
// 슬롯에 드롭하면 그 위치로 이동한다.
// 반환: true 면 원소 추가/삭제/재정렬로 상태가 변했음.
// 헤더 전용 템플릿이라 별도 cpp 없이 사용 가능.
//
// std::vector 전용 얇은 래퍼다 — 실제 UI 는 위의 ImListVirtual 이 그린다.
template <typename T, typename TDrawRowFunc>
bool ImList(const char* id, std::vector<T>& items,
	TDrawRowFunc&& drawRow, T defaultValue = T{}, EImListFlags flags = IMLIST_FLAGS_NONE)
{
    return ImListVirtual(id, static_cast<int>(items.size()),
        // 행 편집 여부는 기존 계약대로 반환에 반영하지 않는다(추가/삭제/재정렬만 변경으로 본다).
        [&](int index) -> bool { drawRow(items[index], index); return false; },
        [&]() { items.push_back(defaultValue); },
        [&](int index) { items.erase(items.begin() + index); },
        [&](int fromIndex, int toIndex)
        {
            T moved = std::move(items[fromIndex]);
            items.erase(items.begin() + fromIndex);
            items.insert(items.begin() + toIndex, std::move(moved));
        },
        flags);
}
