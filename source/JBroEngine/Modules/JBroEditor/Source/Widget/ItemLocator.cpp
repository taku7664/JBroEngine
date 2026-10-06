#include <JBro/Editor/Widget/ItemLocator.h>

#include <imgui.h>
#include <imgui_internal.h>

namespace JBro::Widget::ItemLocator
{
    namespace
    {
        ImGuiID g_watched = 0;
        ImGuiContext* g_context = nullptr;
        Bool g_found = false;
        ImRect g_rect;
    }

    void Watch(UInt32 id)
    {
        g_watched = static_cast<ImGuiID>(id.Get());
        g_context = ImGui::GetCurrentContext();
        g_found = false;
        if (g_context != nullptr)
        {
            g_context->TestEngineHookItems = true;
        }
    }

    Bool Take(ItemRect& rect)
    {
        if (g_context != nullptr)
        {
            g_context->TestEngineHookItems = false;
        }
        g_watched = 0;
        g_context = nullptr;
        if (false == g_found)
        {
            return false;
        }
        rect.minX = g_rect.Min.x;
        rect.minY = g_rect.Min.y;
        rect.maxX = g_rect.Max.x;
        rect.maxY = g_rect.Max.y;
        return true;
    }

    Bool IsWatching()
    {
        return g_watched != 0 && g_context == ImGui::GetCurrentContext();
    }

    void OnItemAdd(ImGuiContext* context, ImGuiID id, const ImRect& rect);

    void Report(UInt32 id, const ItemRect& rect)
    {
        OnItemAdd(ImGui::GetCurrentContext(), static_cast<ImGuiID>(id.Get()), ImRect(rect.minX, rect.minY, rect.maxX, rect.maxY));
    }

    // ImGui 가 항목을 더할 때 부른다. 클립 검사 전이라 창의 클립 영역과 겹쳐 보이는 부분만 적는다 - 다 가려졌으면 적지 않는다.
    void OnItemAdd(ImGuiContext* context, ImGuiID id, const ImRect& rect)
    {
        if (id == 0 || id != g_watched || context != g_context || context->CurrentWindow == nullptr)
        {
            return;
        }
        ImRect visible = rect;
        visible.ClipWith(context->CurrentWindow->ClipRect);
        if (visible.GetWidth() <= 0.0f || visible.GetHeight() <= 0.0f)
        {
            return;
        }
        g_rect = visible;
        g_found = true;
    }
}

// ImGui 의 시험 엔진 훅이다(`imconfig.h` 의 `IMGUI_ENABLE_TEST_ENGINE`). 이름과 꼴은 ImGui 가 정한다. 우리는 항목의 자리만 쓴다.
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* item_data)
{
    (void)item_data;
    JBro::Widget::ItemLocator::OnItemAdd(ctx, id, bb);
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags)
{
    (void)ctx;
    (void)id;
    (void)label;
    (void)flags;
}

void ImGuiTestEngineHook_Log(ImGuiContext* ctx, const char* fmt, ...)
{
    (void)ctx;
    (void)fmt;
}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext* ctx, ImGuiID id)
{
    (void)ctx;
    (void)id;
    return nullptr;
}
