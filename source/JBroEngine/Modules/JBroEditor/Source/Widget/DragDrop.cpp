#include <JBro/Editor/Widget/DragDrop.h>

#include <imgui.h>
#include <imgui_internal.h>

namespace JBro::Widget
{
    namespace
    {
        // ImGui 가 꾸러미를 가리는 이름이다. 32 자를 넘으면 ImGui 가 단언한다.
        // 옛 이름을 그대로 둔다 - 바꿀 까닭이 없고, 바꾸면 끌던 중의 꾸러미를 읽지 못한다.
        constexpr const char* KindNames[] = {
            "JBRO_ASSET",
            "JBRO_HIERARCHY_MOVE",
            "JBRO_HIERARCHY_LAYER",
            "JBRO_LIST_REORDER",
        };
        static_assert(sizeof(KindNames) / sizeof(KindNames[0]) == static_cast<std::size_t>(DragKind::Count),
            "every drag kind needs its payload name");

        const char* NameOf(DragKind kind)
        {
            return KindNames[static_cast<std::size_t>(kind)];
        }
    }

    bool BeginDragSource()
    {
        // **출처도 기본 표시를 끈다.** ImGui 는 출처의 이 깃발을 받는 쪽 전부에 건다 - 받는 자리가 늘어도
        // 한 곳에서 외곽선이 사라진다. 끌고 있는 동안 다른 창의 탭이 열리지 않게도 한다.
        return ImGui::BeginDragDropSource(
            ImGuiDragDropFlags_SourceNoHoldToOpenOthers | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
    }

    void SetDragPayload(DragKind kind, const void* data, std::size_t size)
    {
        ImGui::SetDragDropPayload(NameOf(kind), data, size);
    }

    void EndDragSource()
    {
        ImGui::EndDragDropSource();
    }

    bool IsDragging(DragKind kind)
    {
        const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        return payload != nullptr && payload->IsDataType(NameOf(kind));
    }

    bool IsDraggingAnything()
    {
        return ImGui::GetDragDropPayload() != nullptr;
    }

    DropPayload PeekDrag(DragKind kind)
    {
        DropPayload result;
        const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        if (payload != nullptr && payload->Data != nullptr && payload->IsDataType(NameOf(kind)))
        {
            result.data = payload->Data;
            result.size = static_cast<std::size_t>(payload->DataSize);
        }
        return result;
    }

    bool BeginDropTarget()
    {
        return ImGui::BeginDragDropTarget();
    }

    DropPayload AcceptDrop(DragKind kind, DropFeedback feedback)
    {
        // 놓기 전에도 꾸러미를 받아 위에 있는지를 안다. 기본 표시(테두리)는 끈다.
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(NameOf(kind),
            ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        DropPayload result;
        if (payload == nullptr || payload->Data == nullptr)
        {
            return result;
        }
        result.data = payload->Data;
        result.size = static_cast<std::size_t>(payload->DataSize);
        result.delivered = payload->IsDelivery();
        // `Preview` 는 지난 프레임에도 이 자리가 받았다는 뜻이다. 겹친 자리 가운데 가장 작은 것 하나만 칠해진다.
        if (feedback == DropFeedback::Fill && payload->IsPreview())
        {
            ImGuiContext& g = *ImGui::GetCurrentContext();
            ImRect rect = g.DragDropTargetRect;
            rect.ClipWith(g.DragDropTargetClipRect);
            DrawDropFill(rect.Min.x, rect.Min.y, rect.Max.x, rect.Max.y);
        }
        return result;
    }

    void EndDropTarget()
    {
        ImGui::EndDragDropTarget();
    }

    void DrawDropFill(float minX, float minY, float maxX, float maxY)
    {
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(minX, minY), ImVec2(maxX, maxY),
            ImGui::GetColorU32(ImGuiCol_DragDropTargetBg), ImGui::GetStyle().DragDropTargetRounding);
    }

    void DrawDropLine(float minX, float maxX, float y)
    {
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(minX, y), ImVec2(maxX, y), ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
    }
}
