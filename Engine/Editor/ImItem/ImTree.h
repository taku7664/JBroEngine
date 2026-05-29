#pragma once

struct ImTreeDrawContext
{
    ImRect RowRect;
    ImRect ContentRect;
    bool IsOpen = false;
    bool IsSelected = false;
    bool IsVisible = false;
};

bool ImTree(const char* label, ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None);
bool ImTreeBegin(
    const char* id,
    ImGuiTreeNodeFlags flags,
    ImTreeDrawContext* outContext = nullptr
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
