#pragma once

#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/Widget/Common.h>

#include <JBro/Types/Array.h>

#include <cstdio>
#include <type_traits>
#include <utility>

namespace JBro::Widget
{
    enum ListFlags : std::uint32_t
    {
        ListFlagsNone = 0,
        ListFlagsReadOnly = 1u << 0,
        // 추가·삭제·편집은 되되 **끌어서 순서 바꾸기만** 막는다. 저장소에 순서
        // 개념이 없는 것(표 등)용이다 - 읽기 전용으로는 편집까지 함께 막힌다.
        ListFlagsNoReorder = 1u << 1,
        // 핸들 옆에 [0] [1] … 번호를 붙인다. 순서가 뜻을 갖는 목록용이다.
        // 기본으로 켜지 않는 이유: 순서 없는 목록에서는 번호가 거짓이 되고,
        // 가로가 빠듯한 목록에서는 내용 폭만 깎는다.
        ListFlagsShowIndex = 1u << 2,
    };

    // 맨 아래 "추가" 자리를 부르는 쪽이 그리지 않을 때 쓰는 빈 표시다.
    struct NoAddRow
    {
    };

    // **저장소를 모르는 목록이다**(ProjectRule §11.1).
    //
    // 원소 접근이 전부 콜백이라 `Array<T>` 가 아닌 것 - 타입이 지워져
    // `void*` 와 함수 포인터로만 만질 수 있는 리플렉션 배열 - 도 같은 UI 로 그린다.
    // 기존 엔진 `ImListVirtual` 을 옮긴 것이고, 옮기면서 아이콘 글리프만 글자로
    // 바꾸었다(아이콘 글꼴이 아직 없다).
    //
    //   drawRow(index)        -> bool. 그 행을 그리고, 값이 바뀌었으면 참.
    //   addElement()          -> 맨 뒤에 기본값 하나.
    //   removeElement(index)  -> 그 원소를 지운다.
    //   moveElement(from, to) -> from 을 빼서 to **자리에 끼운다**. to 는 이미
    //                            보정된 원소 번호다 - 슬롯 번호가 아니다.
    //
    // 돌려주는 값: 참이면 추가·삭제·재정렬 또는 행 편집으로 무언가 바뀌었다.
    template <typename TDrawRow, typename TAdd, typename TRemove, typename TMove,
        typename TDrawAddRow = NoAddRow>
    bool ListVirtual(
        const char* id,
        int count,
        TDrawRow&& drawRow,
        TAdd&& addElement,
        TRemove&& removeElement,
        TMove&& moveElement,
        std::uint32_t flags = ListFlagsNone,
        TDrawAddRow&& drawAddRow = NoAddRow{})
    {
        constexpr bool hasCustomAddRow =
            false == std::is_same_v<std::decay_t<TDrawAddRow>, NoAddRow>;

        ImGuiStyle& style = ImGui::GetStyle();
        const bool readOnly = (flags & ListFlagsReadOnly) != 0;
        // 읽기 전용이면 당연히 재정렬도 안 되고, 재정렬 금지면 편집만 열어 둔다.
        const bool reorderable =
            (false == readOnly) && ((flags & ListFlagsNoReorder) == 0);

        // 번호 칸의 폭. 자릿수가 늘어도 내용 시작이 흔들리지 않도록 **가장 긴 번호**
        // 기준으로 한 번 재서 모든 행에 같은 폭을 쓴다(행마다 재면 9→10 에서 칸이 튄다).
        const bool showIndex = (flags & ListFlagsShowIndex) != 0;
        float indexWidth = 0.0f;
        if (showIndex)
        {
            char widest[16] = {};
            std::snprintf(widest, sizeof(widest), "[%d]", count > 0 ? count - 1 : 0);
            indexWidth = ImGui::CalcTextSize(widest).x + style.ItemSpacing.x;
        }

        bool changed = false;
        ImGui::PushID(id);

        // 행 간격을 최소로. 목록은 빽빽해야 한눈에 들어온다.
        StyleScope compact;
        compact.PushVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 1.0f));
        compact.PushVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 2.0f));

        const ImGuiChildFlags childFlags =
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY;
        ImGui::BeginChild("##list_body", ImVec2(0.0f, 0.0f), childFlags);

        // 왼쪽 손잡이와 오른쪽 삭제 표시. **아이콘 글꼴이 아직 없어 글자로 쓴다** -
        // 기존 엔진은 FontAwesome 글리프였다. 폭을 고정해 두는 것은 같다.
        constexpr const char* RowHandleGlyph = "=";
        constexpr const char* RowRemoveGlyph = "x";
        constexpr float RowHandleWidth = 14.0f;
        constexpr float RowRemoveWidth = 22.0f;
        constexpr float SlotHeight = 3.0f;
        // 끌어 놓기 꾸러미 이름. 한 목록 안에서만 받아야 하므로 부르는 쪽의
        // id 아래(`PushID`)에서만 유효하다.
        constexpr const char* DragPayload = "JBRO_LIST_REORDER";

        int removeIndex = -1;
        int moveFrom = -1;
        int moveTo = -1;

        auto drawDropSlot = [&](int slotIndex) {
            StyleScope slotStyle;
            slotStyle.PushVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));
            // 행 사이의 얇은 빈 자리. 여기에 떨어뜨리면 그 자리로 옮긴다.
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            ImGui::PushID(slotIndex);
            ImGui::InvisibleButton("##slot", ImVec2(width, SlotHeight));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragPayload))
                {
                    moveFrom = *static_cast<const int*>(payload->Data);
                    moveTo = slotIndex;
                }
                // 떨어뜨릴 자리를 선으로 보여 준다. 없으면 어디로 가는지 모른다.
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(cursor.x, cursor.y + SlotHeight * 0.5f),
                    ImVec2(cursor.x + width, cursor.y + SlotHeight * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        };

        for (int index = 0; index < count; ++index)
        {
            // 재정렬을 안 받으면 떨어뜨릴 자리 자체를 두지 않는다.
            if (reorderable)
            {
                drawDropSlot(index);
            }

            ImGui::PushID(index);

            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float frameHeight = ImGui::GetFrameHeight();
            const float contentWidth =
                avail.x - RowHandleWidth - RowRemoveWidth - indexWidth - 8.0f;
            const ImVec2 bodyStart = ImGui::GetCursorPos();

            // **손잡이만 잡아야 끌린다.** 행 전체를 끌리게 두면 안의 글자 칸을
            // 고치려고 누른 것이 끌기로 바뀐다.
            ImGui::Selectable("##row_body", false, ImGuiSelectableFlags_AllowOverlap,
                ImVec2(avail.x, frameHeight));
            if (reorderable)
            {
                StyleScope dragStyle;
                dragStyle.PushVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
                dragStyle.PushVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                dragStyle.PushVar(ImGuiStyleVar_WindowRounding, 0.0f);
                const ImGuiDragDropFlags dragFlags =
                    ImGuiDragDropFlags_AcceptNoDrawDefaultRect
                    | ImGuiDragDropFlags_SourceNoHoldToOpenOthers;
                if (ImGui::BeginDragDropSource(dragFlags))
                {
                    dragStyle.Pop();
                    int source = index;
                    ImGui::SetDragDropPayload(DragPayload, &source, sizeof(int));
                    {
                        // 끌고 다니는 그림은 만질 수 없어야 한다.
                        DisableScope disable;
                        drawRow(index);
                    }
                    ImGui::EndDragDropSource();
                }
            }
            const ImVec2 bodyEnd = ImGui::GetCursorPos();
            ImGui::SetCursorPos(bodyStart);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(RowHandleGlyph);
            ImGui::SameLine();

            if (showIndex)
            {
                // **여기서 `AlignTextToFramePadding` 을 다시 부르거나 커서 Y 를
                // 만지면 안 된다.** 그 함수는 커서를 옮기는 것이 아니라 이 줄의
                // 글자 기준선을 세우는 것이고, 손잡이를 그릴 때 이미 세워졌다.
                // 같은 줄에서 Y 를 건드리면 번호만 아래로 밀린다.
                char text[16] = {};
                std::snprintf(text, sizeof(text), "[%d]", index);
                const float startX = ImGui::GetCursorPosX();
                ImGui::TextDisabled("%s", text);
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::SetCursorPosX(startX + indexWidth);
            }

            ImGui::BeginGroup();
            ImGui::PushItemWidth(contentWidth);
            if (readOnly)
            {
                DisableScope disable;
                drawRow(index);
            }
            else
            {
                changed = drawRow(index) || changed;
            }
            ImGui::PopItemWidth();
            ImGui::EndGroup();

            if (false == readOnly)
            {
                ImGui::SameLine();
                if (TextButton(RowRemoveGlyph, ImVec2(0.0f, 0.0f), ImVec2(0.0f, -1.0f)))
                {
                    removeIndex = index;
                }
                HoveredTooltip(Loc::TextOr(LocKeys::ListRemoveElement, "Remove element"));
            }
            ImGui::PopID();
            ImGui::SetCursorPos(bodyEnd);
        }

        if (reorderable)
        {
            // 마지막 뒤의 자리. 맨 끝으로 옮기는 길이다.
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
            // **슬롯 번호는 "이 원소 앞" 을 뜻한다.** 원본을 먼저 빼내므로 뒤로
            // 옮길 때는 목표가 한 칸 당겨진다 - 보정을 여기서 끝내고 콜백에는
            // 최종 원소 번호만 넘긴다. 콜백마다 같은 실수를 반복하지 않도록.
            int target = moveTo;
            if (moveFrom < target)
            {
                --target;
            }
            moveElement(moveFrom, target);
            changed = true;
        }

        if (false == readOnly)
        {
            if constexpr (hasCustomAddRow)
            {
                // 보통 행과 같은 자리에 오도록 손잡이 폭만큼 들여쓴다.
                const float addAvail = ImGui::GetContentRegionAvail().x;
                const float addContent = addAvail - RowHandleWidth - RowRemoveWidth - 8.0f;
                ImGui::Dummy(ImVec2(ImGui::CalcTextSize(RowHandleGlyph).x, 0.0f));
                ImGui::SameLine();
                ImGui::PushItemWidth(addContent);
                changed = drawAddRow() || changed;
                ImGui::PopItemWidth();
            }
            else
            {
                if (ImGui::Selectable(Loc::TextOr(LocKeys::ListAddElement, "Add element")))
                {
                    addElement();
                    changed = true;
                }
            }
        }
        // **마지막에 빈 항목 하나를 둔다.**
        //
        // 행마다 끝에서 커서를 손으로 옮기는데(`SetCursorPos(bodyEnd)`), 그 뒤에
        // 아무것도 그리지 않으면 ImGui 가 "항목 없이 경계만 늘렸다" 고 단언한다.
        // 보통은 끌어놓기 자리나 추가 줄이 뒤에 오지만, **읽기 전용이면서 재정렬도
        // 막힌 목록**에는 둘 다 없다 - 그때만 터진다.
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        ImGui::EndChild();
        ImGui::PopID();
        compact.Pop();
        return changed;
    }

    // `Array<T>` 를 위한 얇은 덮개다. 그리는 일은 전부 위쪽이 한다.
    template <typename T, typename TDrawRow>
    bool List(
        const char* id,
        Array<T>& items,
        TDrawRow&& drawRow,
        T defaultValue = T{},
        std::uint32_t flags = ListFlagsNone)
    {
        return ListVirtual(id, static_cast<int>(items.Size()),
            // 행 편집은 변경으로 세지 않는다 - 추가·삭제·재정렬만 본다.
            // 기존 엔진과 같은 계약이다.
            [&](int index) -> bool { drawRow(items[static_cast<std::size_t>(index)], index); return false; },
            [&]() { items.Add(defaultValue); },
            [&](int index) {
                for (std::size_t at = static_cast<std::size_t>(index) + 1;
                    at < items.Size(); ++at)
                {
                    items[at - 1] = std::move(items[at]);
                }
                items.Resize(items.Size() - 1);
            },
            [&](int fromIndex, int toIndex) {
                T moved = std::move(items[static_cast<std::size_t>(fromIndex)]);
                if (fromIndex < toIndex)
                {
                    for (int at = fromIndex; at < toIndex; ++at)
                    {
                        items[static_cast<std::size_t>(at)] =
                            std::move(items[static_cast<std::size_t>(at) + 1]);
                    }
                }
                else
                {
                    for (int at = fromIndex; at > toIndex; --at)
                    {
                        items[static_cast<std::size_t>(at)] =
                            std::move(items[static_cast<std::size_t>(at) - 1]);
                    }
                }
                items[static_cast<std::size_t>(toIndex)] = std::move(moved);
            },
            flags);
    }
}
