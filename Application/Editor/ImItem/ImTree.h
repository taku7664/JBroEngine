#pragma once

#include "ThirdParty/imgui/imgui.h"          // ImRect, ImGuiTreeNodeFlags, ImVec2
#include "ThirdParty/imgui/imgui_internal.h" // ImRect

struct ImTreeDrawContext
{
    ImRect RowRect;
    ImRect ContentRect;
    bool IsOpen = false;
    bool IsSelected = false;
    bool IsVisible = false;
};

bool ImTree(const char* label, ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None);
// minContentHeight: 컨텐츠 영역의 최소 높이(px). 기본 0 이면 한 줄 높이(폰트) 그대로.
// 썸네일처럼 텍스트보다 높은 것을 ContentRect 에 그릴 때 행 높이를 그만큼 키운다.
bool ImTreeBegin(
    const char* id,
    ImGuiTreeNodeFlags flags,
    ImTreeDrawContext* outContext = nullptr,
    float minContentHeight = 0.0f
);
void ImTreeEnd();

namespace ImTreeInternal
{
    template <typename TDrawer>
    void InvokeDrawers(TDrawer&& drawer)
    {
        drawer();
    }

    template <typename TDrawer, typename... TDrawers>
    void InvokeDrawers(TDrawer&& drawer, TDrawers&&... drawers)
    {
        drawer();
        ((ImGui::SameLine(), drawers()), ...);
    }
}

template <typename... TDrawers>
bool ImTreeEx(
    const char* id,
    ImGuiTreeNodeFlags flags,
    TDrawers&&... drawers
)
{
    ImTreeDrawContext context;
    const bool isOpen = ImTreeBegin(id, flags, &context);
    ImTreeEnd();

    if (context.IsVisible)
    {
        if constexpr (sizeof...(drawers) > 0)
        {
            const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(context.ContentRect.Min);
            ImTreeInternal::InvokeDrawers(drawers...);
            ImGui::SetCursorScreenPos(cursorPos);
        }
    }

    return isOpen;
}
