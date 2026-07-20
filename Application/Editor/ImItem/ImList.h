#pragma once

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
template <typename TDrawRowFunc, typename TAddFunc, typename TRemoveFunc, typename TMoveFunc>
bool ImListVirtual(const char* id, int count,
    TDrawRowFunc&& drawRow, TAddFunc&& addElement, TRemoveFunc&& removeElement, TMoveFunc&& moveElement,
    EImListFlags flags = IMLIST_FLAGS_NONE)
{
    ImGuiStyle& style = ImGui::GetStyle();

    const bool readOnly = (0 != (flags & IMLIST_FLAGS_READ_ONLY));

    bool changed = false;
    ImGui::PushID(id);

    // 컴팩트: 행 간격 최소.
    ImGui::Utillity::StyleBuilder compact;
    compact.PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 1.0f));
    compact.PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 2.0f));

    // 박스 외곽
    const ImGuiChildFlags childFlags = ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY;
    ImGui::BeginChild("##list_body", ImVec2(0.0f, 0.0f), childFlags);

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
        // 읽기 전용이면 재정렬도 막아야 하므로 드롭 슬롯 자체를 두지 않는다.
        if (false == readOnly)
        {
            drawDropSlot(i);
        }

        ImGui::PushID(i);

        const ImVec2    availSpace = ImGui::GetContentRegionAvail();
        const float     frameHeight = ImGui::GetFrameHeight();
        const float		rowAvailW = ImGui::GetContentRegionAvail().x;
        const float		contentW = rowAvailW - ROW_HANDLE_W - ROW_REMOVE_W - 8.0f;
        const ImVec2    bodyStartCursor = ImGui::GetCursorPos();

        // 좌측 핸들 — 드래그 소스. 핸들만 잡아야 콘텐츠의 일반 InputText 와
        // 충돌하지 않는다. 행 높이는 콘텐츠(프레임)와 동일하게 — 컴팩트.
        const char* selectableLabel = "\xEF\x83\x89";
        ImVec2 bodySize = ImVec2(availSpace.x, frameHeight);
        ImGui::Selectable("##row_body", false, ImGuiSelectableFlags_AllowOverlap, bodySize);
        if (false == readOnly)
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

    if (false == readOnly)
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
        if (ImGui::Selectable(Loc::Text(ImItemLocKeys::ListAddElement), false, ImGuiSelectableFlags_None))
        {
            addElement();
            changed = true;
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
