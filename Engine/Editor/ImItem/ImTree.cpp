#include "pch.h"
#include "ImTree.h"

using namespace ImGui;

bool ImTreeRender(
    ImGuiID id,
    ImGuiTreeNodeFlags flags,
    const char* label,
    const char* label_end,
    ImTreeDrawContext* outContext,
    bool renderDefaultText
);

namespace
{
    struct ImTreeCursorRestore
    {
        ImVec2 CursorPos;
    };

    ImVector<ImTreeCursorRestore> g_ImTreeCursorRestoreStack;

    void TreeNodeStoreStackData(ImGuiTreeNodeFlags flags, float x1)
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;

        g.TreeNodeStack.resize(g.TreeNodeStack.Size + 1);
        ImGuiTreeNodeStackData* treeNodeData = &g.TreeNodeStack.Data[g.TreeNodeStack.Size - 1];
        treeNodeData->ID = g.LastItemData.ID;
        treeNodeData->TreeFlags = flags;
        treeNodeData->ItemFlags = g.LastItemData.ItemFlags;
        treeNodeData->NavRect = g.LastItemData.NavRect;

        const bool drawLines = (flags & (ImGuiTreeNodeFlags_DrawLinesFull | ImGuiTreeNodeFlags_DrawLinesToNodes)) != 0;
        treeNodeData->DrawLinesX1 = drawLines ? (x1 + g.FontSize * 0.5f + g.Style.FramePadding.x) : +FLT_MAX;
        treeNodeData->DrawLinesTableColumn = (drawLines && g.CurrentTable) ? (ImGuiTableColumnIdx)g.CurrentTable->CurrentColumn : -1;
        treeNodeData->DrawLinesToNodesY2 = -FLT_MAX;

        window->DC.TreeHasStackDataDepthMask |= (1 << window->DC.TreeDepth);
        if (flags & ImGuiTreeNodeFlags_DrawLinesToNodes)
        {
            window->DC.TreeRecordsClippedNodesY2Mask |= (1 << window->DC.TreeDepth);
        }
    }
}

bool ImTree(const char* label, ImGuiTreeNodeFlags flags)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
    {
        return false;
    }

    ImGuiID id = window->GetID(label);
    return ImTreeRender(id, flags, label, nullptr, nullptr, true);
}

bool ImTreeBegin(
    const char* idText,
    ImGuiTreeNodeFlags flags,
    ImTreeDrawContext* outContext
)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
    {
        if (outContext)
        {
            *outContext = ImTreeDrawContext();
        }

        g_ImTreeCursorRestoreStack.push_back(ImTreeCursorRestore{ window->DC.CursorPos });
        return false;
    }

    const char* idLabel = idText ? idText : "";
    ImGuiID id = window->GetID(idLabel);
    ImTreeDrawContext context;
    const bool isOpen = ImTreeRender(id, flags, idLabel, nullptr, &context, false);

    g_ImTreeCursorRestoreStack.push_back(ImTreeCursorRestore{ window->DC.CursorPos });

    if (outContext)
    {
        *outContext = context;
    }

    return isOpen;
}

void ImTreeEnd()
{
    ImGuiWindow* window = GetCurrentWindow();
    IM_ASSERT(g_ImTreeCursorRestoreStack.Size > 0);

    if (g_ImTreeCursorRestoreStack.Size <= 0)
    {
        return;
    }

    const ImTreeCursorRestore restore = g_ImTreeCursorRestoreStack.back();
    g_ImTreeCursorRestoreStack.pop_back();
    window->DC.CursorPos = restore.CursorPos;
}

bool ImTreeRender(
    ImGuiID id,
    ImGuiTreeNodeFlags flags,
    const char* label,
    const char* label_end,
    ImTreeDrawContext* outContext,
    bool renderDefaultText
)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
    {
        if (outContext)
        {
            *outContext = ImTreeDrawContext();
        }

        return false;
    }

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    const bool display_frame = (flags & ImGuiTreeNodeFlags_Framed) != 0;
    const bool use_frame_padding = (display_frame || (flags & ImGuiTreeNodeFlags_FramePadding));
    const ImVec2 padding = use_frame_padding
        ? style.FramePadding
        : ImVec2(style.FramePadding.x, ImMin(window->DC.CurrLineTextBaseOffset, style.FramePadding.y));

    if (renderDefaultText && !label_end)
    {
        label_end = FindRenderedTextEnd(label);
    }

    const ImVec2 label_size = renderDefaultText
        ? CalcTextSize(label, label_end, false)
        : ImVec2(0.0f, g.FontSize);

    ImVec2 content_size = label_size;

    const float text_offset_x = g.FontSize + (display_frame ? padding.x * 2.5f : padding.x * 1.5f);
    const float text_offset_y = use_frame_padding
        ? ImMax(style.FramePadding.y, window->DC.CurrLineTextBaseOffset)
        : window->DC.CurrLineTextBaseOffset;

    const float text_width = renderDefaultText
        ? g.FontSize + content_size.x + padding.x * 2.0f
        : g.FontSize + padding.x * 2.0f;

    const float frame_height = ImMax(label_size.y, content_size.y) + padding.y * 2.0f + 3.0f;

    const bool span_all_columns =
        (flags & ImGuiTreeNodeFlags_SpanAllColumns) != 0 && (g.CurrentTable != NULL);

    const bool span_all_columns_label =
        (flags & ImGuiTreeNodeFlags_LabelSpanAllColumns) != 0 && (g.CurrentTable != NULL);

    const float tree_depth_indent_compensation = static_cast<float>(ImMax(window->DC.TreeDepth, 0)) * style.IndentSpacing * 0.5f;
    const float row_cursor_x = window->DC.CursorPos.x - tree_depth_indent_compensation;
    const float row_cursor_y = window->DC.CursorPos.y;

    ImRect frame_bb;

    // 렌더 박스는 자식 깊이와 관계없이 항상 한 줄 전체를 사용.
    // 즉, 자식 노드여도 hover/selected 배경이 왼쪽부터 일렬로 맞음.
    frame_bb.Min.x = span_all_columns ? window->ParentWorkRect.Min.x : window->WorkRect.Min.x;
    frame_bb.Min.y = row_cursor_y + (text_offset_y - padding.y);
    frame_bb.Max.x = span_all_columns ? window->ParentWorkRect.Max.x : window->WorkRect.Max.x;
    frame_bb.Max.y = frame_bb.Min.y + frame_height;

    if (display_frame)
    {
        const float outer_extend = IM_TRUNC(window->WindowPadding.x * 0.5f);
        frame_bb.Min.x -= outer_extend;
        frame_bb.Max.x += outer_extend;
    }

    ImVec2 text_pos(
        row_cursor_x + text_offset_x,
        frame_bb.Min.y + (frame_height - content_size.y) * 0.5f
    );
    const float arrow_center_y = frame_bb.Min.y + frame_height * 0.5f;

    if (!renderDefaultText)
    {
        content_size.x = ImMax(0.0f, frame_bb.Max.x - text_pos.x);
    }

    ItemSize(ImVec2(text_width, frame_height), padding.y);

    // row 사이 세로 갭 제거.
    // ItemSize()가 다음 CursorPos.y에 style.ItemSpacing.y를 반영하므로 다시 빼준다.
    if (style.ItemSpacing.y != 0.0f)
    {
        window->DC.CursorPos.y -= style.ItemSpacing.y;
    }

    ImRect interact_bb;
    interact_bb.Min.x = row_cursor_x;
    interact_bb.Min.y = frame_bb.Min.y;
    interact_bb.Max.x = renderDefaultText
        ? text_pos.x + content_size.x + padding.x
        : frame_bb.Max.x;
    interact_bb.Max.y = frame_bb.Max.y;

    const float min_interact_width = g.FontSize + padding.x * 2.0f;
    if (interact_bb.Max.x < interact_bb.Min.x + min_interact_width)
    {
        interact_bb.Max.x = interact_bb.Min.x + min_interact_width;
    }

    ImGuiID storage_id =
        (g.NextItemData.HasFlags & ImGuiNextItemDataFlags_HasStorageID)
        ? g.NextItemData.StorageId
        : id;

    bool is_open = TreeNodeUpdateNextOpen(storage_id, flags);
    bool selected = (flags & ImGuiTreeNodeFlags_Selected) != 0;

    if (outContext)
    {
        outContext->RowRect = frame_bb;
        outContext->ContentRect = ImRect(text_pos, text_pos + content_size);
        outContext->IsOpen = is_open;
        outContext->IsSelected = selected;
        outContext->IsVisible = false;
    }

    bool is_visible;
    if (span_all_columns || span_all_columns_label)
    {
        const float backup_clip_rect_min_x = window->ClipRect.Min.x;
        const float backup_clip_rect_max_x = window->ClipRect.Max.x;

        window->ClipRect.Min.x = window->ParentWorkRect.Min.x;
        window->ClipRect.Max.x = window->ParentWorkRect.Max.x;

        is_visible = ItemAdd(interact_bb, id);

        window->ClipRect.Min.x = backup_clip_rect_min_x;
        window->ClipRect.Max.x = backup_clip_rect_max_x;
    }
    else
    {
        is_visible = ItemAdd(interact_bb, id);
    }

    g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_HasDisplayRect;
    g.LastItemData.DisplayRect = frame_bb;

    bool store_tree_node_stack_data = false;

    if ((flags & ImGuiTreeNodeFlags_DrawLinesMask_) == 0)
    {
        flags |= g.Style.TreeLinesFlags;
    }

    const bool draw_tree_lines =
        (flags & (ImGuiTreeNodeFlags_DrawLinesFull | ImGuiTreeNodeFlags_DrawLinesToNodes)) &&
        (frame_bb.Min.y < window->ClipRect.Max.y) &&
        (g.Style.TreeLinesSize > 0.0f);

    if (!(flags & ImGuiTreeNodeFlags_NoTreePushOnOpen))
    {
        store_tree_node_stack_data = draw_tree_lines;

        if ((flags & ImGuiTreeNodeFlags_NavLeftJumpsToParent) && !g.NavIdIsAlive)
        {
            if (g.NavMoveDir == ImGuiDir_Left && g.NavWindow == window && NavMoveRequestButNoResultYet())
            {
                store_tree_node_stack_data = true;
            }
        }
    }

    const bool is_leaf = (flags & ImGuiTreeNodeFlags_Leaf) != 0;

    if (!is_visible)
    {
        if ((flags & ImGuiTreeNodeFlags_DrawLinesToNodes) &&
            (window->DC.TreeRecordsClippedNodesY2Mask & (1 << (window->DC.TreeDepth - 1))))
        {
            ImGuiTreeNodeStackData* parent_data = &g.TreeNodeStack.Data[g.TreeNodeStack.Size - 1];
            parent_data->DrawLinesToNodesY2 = ImMax(parent_data->DrawLinesToNodesY2, window->DC.CursorPos.y);

            if (frame_bb.Min.y >= window->ClipRect.Max.y)
            {
                window->DC.TreeRecordsClippedNodesY2Mask &= ~(1 << (window->DC.TreeDepth - 1));
            }
        }

        if (is_open && store_tree_node_stack_data)
        {
            TreeNodeStoreStackData(flags, text_pos.x - text_offset_x);
        }

        if (is_open && !(flags & ImGuiTreeNodeFlags_NoTreePushOnOpen))
        {
            TreePushOverrideID(id);
        }

        IMGUI_TEST_ENGINE_ITEM_INFO(
            g.LastItemData.ID,
            label,
            g.LastItemData.StatusFlags |
            (is_leaf ? 0 : ImGuiItemStatusFlags_Openable) |
            (is_open ? ImGuiItemStatusFlags_Opened : 0)
        );

        return is_open;
    }

    if (outContext)
    {
        outContext->IsVisible = true;
    }

    if (span_all_columns || span_all_columns_label)
    {
        TablePushBackgroundChannel();
        g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_HasClipRect;
        g.LastItemData.ClipRect = window->ClipRect;
    }

    ImGuiButtonFlags button_flags = ImGuiTreeNodeFlags_None;

    if (!renderDefaultText)
    {
        button_flags |= ImGuiButtonFlags_AllowOverlap;
    }

    if ((flags & ImGuiTreeNodeFlags_AllowOverlap) ||
        (g.LastItemData.ItemFlags & ImGuiItemFlags_AllowOverlap))
    {
        button_flags |= ImGuiButtonFlags_AllowOverlap;
    }

    if (!is_leaf)
    {
        button_flags |= ImGuiButtonFlags_PressedOnDragDropHold;
    }

    const float arrow_hit_x1 =
        (text_pos.x - text_offset_x) - style.TouchExtraPadding.x;

    const float arrow_hit_x2 =
        (text_pos.x - text_offset_x) +
        (g.FontSize + padding.x * 2.0f) +
        style.TouchExtraPadding.x;

    const bool is_mouse_x_over_arrow =
        (g.IO.MousePos.x >= arrow_hit_x1 && g.IO.MousePos.x < arrow_hit_x2);

    const bool is_multi_select =
        (g.LastItemData.ItemFlags & ImGuiItemFlags_IsMultiSelect) != 0;

    if (is_multi_select)
    {
        flags |= (flags & ImGuiTreeNodeFlags_OpenOnMask_) == 0
            ? ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
            : ImGuiTreeNodeFlags_OpenOnArrow;
    }

    if (is_mouse_x_over_arrow)
    {
        button_flags |= ImGuiButtonFlags_PressedOnClick;
    }
    else if (flags & ImGuiTreeNodeFlags_OpenOnDoubleClick)
    {
        button_flags |= ImGuiButtonFlags_PressedOnClickRelease | ImGuiButtonFlags_PressedOnDoubleClick;
    }
    else
    {
        button_flags |= ImGuiButtonFlags_PressedOnClickRelease;
    }

    if (flags & ImGuiTreeNodeFlags_NoNavFocus)
    {
        button_flags |= ImGuiButtonFlags_NoNavFocus;
    }

    const bool was_selected = selected;

    if (is_multi_select)
    {
        MultiSelectItemHeader(id, &selected, &button_flags);

        if (is_mouse_x_over_arrow)
        {
            button_flags = (button_flags | ImGuiButtonFlags_PressedOnClick) &
                ~ImGuiButtonFlags_PressedOnClickRelease;
        }
    }
    else
    {
        if (window != g.HoveredWindow || !is_mouse_x_over_arrow)
        {
            button_flags |= ImGuiButtonFlags_NoKeyModsAllowed;
        }
    }

    bool hovered = false;
    bool held = false;
    bool pressed = ButtonBehavior(interact_bb, id, &hovered, &held, button_flags);

    // 렌더링용 hover는 full row 기준.
    // 클릭/토글 판정은 위의 ButtonBehavior(interact_bb) 기준.
    const bool row_hovered =
        window == g.HoveredWindow &&
        IsMouseHoveringRect(frame_bb.Min, frame_bb.Max, false);

    bool toggled = false;

    if (!is_leaf)
    {
        if (pressed && g.DragDropHoldJustPressedId != id)
        {
            if ((flags & ImGuiTreeNodeFlags_OpenOnMask_) == 0 ||
                (g.NavActivateId == id && !is_multi_select))
            {
                toggled = true;
            }

            if (flags & ImGuiTreeNodeFlags_OpenOnArrow)
            {
                toggled |= is_mouse_x_over_arrow && !g.NavHighlightItemUnderNav;
            }

            if ((flags & ImGuiTreeNodeFlags_OpenOnDoubleClick) &&
                g.IO.MouseClickedCount[0] == 2)
            {
                toggled = true;
            }
        }
        else if (pressed && g.DragDropHoldJustPressedId == id)
        {
            IM_ASSERT(button_flags & ImGuiButtonFlags_PressedOnDragDropHold);

            if (!is_open)
            {
                toggled = true;
            }
            else
            {
                pressed = false;
            }
        }

        if (g.NavId == id && g.NavMoveDir == ImGuiDir_Left && is_open)
        {
            toggled = true;
            NavClearPreferredPosForAxis(ImGuiAxis_X);
            NavMoveRequestCancel();
        }

        if (g.NavId == id && g.NavMoveDir == ImGuiDir_Right && !is_open)
        {
            toggled = true;
            NavClearPreferredPosForAxis(ImGuiAxis_X);
            NavMoveRequestCancel();
        }

        if (toggled)
        {
            is_open = !is_open;
            window->DC.StateStorage->SetInt(storage_id, is_open);
            g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_ToggledOpen;

            if (outContext)
            {
                outContext->IsOpen = is_open;
            }
        }
    }

    if (is_multi_select)
    {
        bool pressed_copy = pressed && !toggled;
        MultiSelectItemFooter(id, &selected, &pressed_copy);

        if (pressed)
        {
            SetNavID(id, window->DC.NavLayerCurrent, g.CurrentFocusScopeId, interact_bb);
        }
    }

    if (selected != was_selected)
    {
        g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_ToggledSelection;

        if (outContext)
        {
            outContext->IsSelected = selected;
        }
    }

    {
        const ImU32 text_col = GetColorU32(ImGuiCol_Text);

        ImGuiNavRenderCursorFlags nav_render_cursor_flags = ImGuiNavRenderCursorFlags_Compact;

        if (is_multi_select)
        {
            nav_render_cursor_flags |= ImGuiNavRenderCursorFlags_AlwaysDraw;
        }

        if (display_frame)
        {
            const ImU32 bg_col = GetColorU32(
                (held && hovered)
                ? ImGuiCol_HeaderActive
                : row_hovered
                ? ImGuiCol_HeaderHovered
                : ImGuiCol_Header
            );

            RenderFrame(frame_bb.Min, frame_bb.Max, bg_col, true, style.FrameRounding);
            RenderNavCursor(frame_bb, id, nav_render_cursor_flags);

            if (span_all_columns && !span_all_columns_label)
            {
                TablePopBackgroundChannel();
            }

            if (flags & ImGuiTreeNodeFlags_Bullet)
            {
                RenderBullet(
                    window->DrawList,
                    ImVec2(text_pos.x - text_offset_x * 0.60f, text_pos.y + g.FontSize * 0.5f),
                    text_col
                );
            }
            else if (!is_leaf)
            {
                const float arrow_scale = 1.0f;
                const float arrow_pos_y = arrow_center_y - g.FontSize * 0.5f * arrow_scale;
                RenderArrow(
                    window->DrawList,
                    ImVec2(text_pos.x - text_offset_x + padding.x, arrow_pos_y),
                    text_col,
                    is_open
                    ? ((flags & ImGuiTreeNodeFlags_UpsideDownArrow) ? ImGuiDir_Up : ImGuiDir_Down)
                    : ImGuiDir_Right,
                    1.0f
                );
            }
            else
            {
                text_pos.x -= text_offset_x - padding.x;
            }

            if (flags & ImGuiTreeNodeFlags_ClipLabelForTrailingButton)
            {
                frame_bb.Max.x -= g.FontSize + style.FramePadding.x;
            }

            if (g.LogEnabled)
            {
                LogSetNextTextDecoration("###", "###");
            }
        }
        else
        {
            if (row_hovered || selected)
            {
                const ImU32 bg_col = GetColorU32(
                    (held && hovered)
                    ? ImGuiCol_HeaderActive
                    : row_hovered
                    ? ImGuiCol_HeaderHovered
                    : ImGuiCol_Header
                );

                RenderFrame(frame_bb.Min, frame_bb.Max, bg_col, false);
            }

            RenderNavCursor(frame_bb, id, nav_render_cursor_flags);

            if (span_all_columns && !span_all_columns_label)
            {
                TablePopBackgroundChannel();
            }

            if (flags & ImGuiTreeNodeFlags_Bullet)
            {
                RenderBullet(
                    window->DrawList,
                    ImVec2(text_pos.x - text_offset_x * 0.5f, text_pos.y + g.FontSize * 0.5f),
                    text_col
                );
            }
            else if (!is_leaf)
            {
                const float arrow_scale = 0.70f;
                const float arrow_pos_y = arrow_center_y - g.FontSize * 0.5f * arrow_scale;
                RenderArrow(
                    window->DrawList,
                    ImVec2(text_pos.x - text_offset_x + padding.x, arrow_pos_y),
                    text_col,
                    is_open
                    ? ((flags & ImGuiTreeNodeFlags_UpsideDownArrow) ? ImGuiDir_Up : ImGuiDir_Down)
                    : ImGuiDir_Right,
                    0.70f
                );
            }

            if (g.LogEnabled)
            {
                LogSetNextTextDecoration(">", NULL);
            }
        }

        if (draw_tree_lines)
        {
            TreeNodeDrawLineToChildNode(
                ImVec2(text_pos.x - text_offset_x + padding.x, arrow_center_y)
            );
        }

        if (renderDefaultText && display_frame)
        {
            RenderTextClipped(text_pos, frame_bb.Max, label, label_end, &label_size);
        }
        else if (renderDefaultText)
        {
            RenderText(text_pos, label, label_end, false);
        }

        if (span_all_columns_label)
        {
            TablePopBackgroundChannel();
        }
    }

    if (is_open && store_tree_node_stack_data)
    {
        TreeNodeStoreStackData(flags, text_pos.x - text_offset_x);
    }

    if (is_open && !(flags & ImGuiTreeNodeFlags_NoTreePushOnOpen))
    {
        TreePushOverrideID(id);
    }

    IMGUI_TEST_ENGINE_ITEM_INFO(
        id,
        label,
        g.LastItemData.StatusFlags |
        (is_leaf ? 0 : ImGuiItemStatusFlags_Openable) |
        (is_open ? ImGuiItemStatusFlags_Opened : 0)
    );

    return is_open;
}
