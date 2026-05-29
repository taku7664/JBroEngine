#pragma once

enum EImListFlags
{
    IMLIST_FLAGS_NONE = 0,
    IMLIST_FLAGS_READ_ONLY = 1 << 0,
};

// ── List ────────────────────────────────────────────────────────────
// 마지막 하단 + / - 버튼.  좌측의 핸들(=) 을 잡아 다른 원소 사이의
// 슬롯에 드롭하면 그 위치로 이동한다.
// 반환: true 면 원소 추가/삭제/재정렬로 상태가 변했음.
// 헤더 전용 템플릿이라 별도 cpp 없이 사용 가능.
template <typename T, typename TDrawRowFunc>
bool ImList(const char* id, std::vector<T>& items,
	TDrawRowFunc&& drawRow, T defaultValue = T{}, EImListFlags flags = IMLIST_FLAGS_NONE)
{
    ImGuiContext* context = ImGui::GetCurrentContext();
    ImGuiStyle& style = ImGui::GetStyle();

    const float fontSize = ImGui::GetFontSize();

    bool changed = false;
    ImGui::PushID(id);

    // 박스 외곽
    const ImGuiChildFlags childFlags = ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY;
    ImGui::BeginChild("##list_body", ImVec2(0.0f, 0.0f), childFlags);

    constexpr float ROW_HANDLE_W = 14.0f;
    constexpr float ROW_REMOVE_W = 22.0f;
    constexpr float SLOT_HEIGHT = 6.0f;
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
                // hover 시 시각적 가이드 라인
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(cursor.x, cursor.y + SLOT_HEIGHT * 0.5f),
                    ImVec2(cursor.x + availW, cursor.y + SLOT_HEIGHT * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        };

    for (int i = 0; i < static_cast<int>(items.size()); ++i)
    {
        drawDropSlot(i);

        ImGui::PushID(i);

        const ImVec2    availSpace = ImGui::GetContentRegionAvail();
        const float     frameHeight = ImGui::GetFrameHeight();
        const float		rowAvailW = ImGui::GetContentRegionAvail().x;
        const float		contentW = rowAvailW - ROW_HANDLE_W - ROW_REMOVE_W - 8.0f;
        const ImVec2    bodyStartCursor = ImGui::GetCursorPos() + style.WindowPadding;

        // 좌측 핸들 — 드래그 소스. 핸들만 잡아야 콘텐츠의 일반 InputText 와
        // 충돌하지 않는다.
        const char* selectableLabel = "=";
        ImVec2 bodySize = ImVec2(availSpace.x, frameHeight + style.WindowPadding.y * 2);
        ImGui::Selectable("##row_body", false, ImGuiSelectableFlags_AllowOverlap, bodySize);
        {	// DragDrop Start
            ImGui::Utillity::StyleBuilder styleBuilder;
            styleBuilder.PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            styleBuilder.PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            styleBuilder.PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            auto flags = ImGuiDragDropFlags_AcceptNoDrawDefaultRect | ImGuiDragDropFlags_SourceNoHoldToOpenOthers;
            if (ImGui::BeginDragDropSource(flags))
            {
                styleBuilder.PopStyle();
                int srcIdx = i;
                ImGui::SetDragDropPayload(DRAG_PAYLOAD, &srcIdx, sizeof(int));
                {
                    ImGui::Utillity::DisableScope disable;
                    drawRow(items[i], i);
                }
                ImGui::EndDragDropSource();
            }
        }
        const ImVec2 bodyEndCursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(bodyStartCursor);
        ImGui::TextUnformatted(selectableLabel);
        ImGui::SameLine();

        // 컨텐츠 영역
        ImGui::BeginGroup();
        ImGui::PushItemWidth(contentW);
        drawRow(items[i], i);
        ImGui::PopItemWidth();
        ImGui::EndGroup();

        // 우측 삭제 버튼
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ROW_REMOVE_W);
        {   // x button
            const char* text = "X";
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            const ImVec2 startCursor = ImGui::GetCursorPos() + style.FramePadding;

            bool isHovered = false;
            {
                ImGui::Utillity::StyleBuilder styleBuilder;
                //styleBuilder.PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                //styleBuilder.PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
                //styleBuilder.PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
                if (ImGui::InvisibleButton("##x_button", textSize + style.FramePadding * 2))
                {
                    removeIndex = i;
                }
                isHovered = ImGui::IsItemHovered();
            }
            {
                ImGui::SetCursorPos(startCursor);
                ImGui::Utillity::StyleBuilder styleBuilder;
                if (isHovered)
                {
                    styleBuilder.PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextSelectedBg]);
                }
                ImGui::TextUnformatted(text);
            }
        }
        ImGui::PopID();
        ImGui::SetCursorPos(bodyEndCursor);
    }
    // 마지막 원소 뒤의 슬롯 (맨 끝으로 이동)
    drawDropSlot(static_cast<int>(items.size()));

    if (removeIndex >= 0)
    {
        items.erase(items.begin() + removeIndex);
        changed = true;
    }
    else if (moveFrom >= 0 && moveTo >= 0
        && moveFrom != moveTo && moveFrom + 1 != moveTo)
    {
        T tmp = std::move(items[moveFrom]);
        items.erase(items.begin() + moveFrom);
        if (moveFrom < moveTo) --moveTo;
        items.insert(items.begin() + moveTo, std::move(tmp));
        changed = true;
    }

    // 하단 + / - 버튼 직전에 한 줄 분량의 약간의 공간을 두어
    // 마지막 슬롯과 버튼이 너무 붙지 않도록 한다.
    if (ImGui::Selectable("+ Add Element", false, ImGuiSelectableFlags_None))
    {
        items.push_back(defaultValue);
        changed = true;
    }
    ImGui::EndChild();
    ImGui::PopID();
    return changed;
}
