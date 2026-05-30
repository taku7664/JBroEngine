#include "pch.h"
#include "ImSplitter.h"

#include "Editor/ImGuiUtillity.h"   // ImGui::Utillity::StyleBuilder, IDGroup

ImVerticalSplitter::ImVerticalSplitter()
{}

ImVerticalSplitter& ImVerticalSplitter::SetThickness(float t)
{
    m_thickness = t;
    return *this;
}

ImVerticalSplitter& ImVerticalSplitter::SetMinPixels(float px)
{
    m_minPixels = px;
    return *this;
}

bool ImVerticalSplitter::operator()(const char* id, float& position, ImVec2 regionMin, ImVec2 regionSize)
{
    // Use SameLine + Button approach (user preference)
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::Utillity::IDGroup idGroup(id);

    ImGui::Utillity::StyleBuilder styleBuilder;
    styleBuilder.PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    styleBuilder.PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.6f, 1.0f, 0.3f));
    styleBuilder.PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.6f, 1.0f, 0.5f));

    ImGui::Button("##InspSplitter", ImVec2(m_thickness, regionSize.y));
    ImGui::SameLine(0.0f, 0.0f);

    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID grabOffsetKey = ImGui::GetID("##VerticalSplitterGrabOffsetX");
    const ImGuiID containerMinKey = ImGui::GetID("##VerticalSplitterContainerMinX");
    const ImGuiID dragWidthKey = ImGui::GetID("##VerticalSplitterDragWidth");

    if (ImGui::IsItemActivated())
    {
        const ImVec2 splitterMin = ImGui::GetItemRectMin();

        const float mouseX = ImGui::GetMousePos().x;

        // where inside the splitter the user grabbed
        const float grabOffsetX = mouseX - splitterMin.x;

        // compute container start X from current pixel position
        const float containerMinX = splitterMin.x - position;

        storage->SetFloat(grabOffsetKey, grabOffsetX);
        storage->SetFloat(containerMinKey, containerMinX);
        storage->SetFloat(dragWidthKey, regionSize.x);
    }

    if (ImGui::IsItemActive())
    {
        const float mouseX = ImGui::GetMousePos().x;

        const float grabOffsetX = storage->GetFloat(grabOffsetKey, 0.0f);
        const float containerMinX = storage->GetFloat(containerMinKey, 0.0f);
        const float dragWidth = std::max(storage->GetFloat(dragWidthKey, regionSize.x), 1.0f);

        float newPos = (mouseX - grabOffsetX - containerMinX);
        const float minP = m_minPixels;
        const float maxP = std::max(dragWidth - m_minPixels, m_minPixels);
        newPos = std::clamp(newPos, minP, maxP);
        position = newPos;

        return true;
    }

    return false;
}

bool VerticalSplitter(const char* id, float& position, ImVec2 regionMin, ImVec2 regionSize, float thickness)
{
    ImVerticalSplitter s;
    s.SetThickness(thickness);
    return s(id, position, regionMin, regionSize);
}

bool HorizontalSplitter(const char* id, float& position, ImVec2 regionMin, ImVec2 regionSize, float thickness)
{
    ImHorizontalSplitter s;
    s.SetThickness(thickness);
    return s(id, position, regionMin, regionSize);
}

// ImHorizontalSplitter implementation
ImHorizontalSplitter::ImHorizontalSplitter()
{}

ImHorizontalSplitter& ImHorizontalSplitter::SetThickness(float t)
{
    m_thickness = t;
    return *this;
}

ImHorizontalSplitter& ImHorizontalSplitter::SetMinPixels(float px)
{
    m_minPixels = px;
    return *this;
}

bool ImHorizontalSplitter::operator()(const char* id, float& position, ImVec2 regionMin, ImVec2 regionSize)
{
    ImGui::Utillity::IDGroup idGroup(id);

    ImGui::Utillity::StyleBuilder styleBuilder;
    styleBuilder.PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    styleBuilder.PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.6f, 1.0f, 0.3f));
    styleBuilder.PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.6f, 1.0f, 0.5f));

    ImGui::SetCursorScreenPos(ImVec2(regionMin.x, regionMin.y + position));
    ImGui::InvisibleButton("##InspSplitterH", ImVec2(regionSize.x, m_thickness));

    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }

    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID grabOffsetKey = ImGui::GetID("##HorizontalSplitterGrabOffsetY");
    const ImGuiID containerMinKey = ImGui::GetID("##HorizontalSplitterContainerMinY");

    if (ImGui::IsItemActivated())
    {
        const ImVec2 splitterMin = ImGui::GetItemRectMin();
        const float mouseY = ImGui::GetMousePos().y;
        const float grabOffsetY = mouseY - splitterMin.y;

        storage->SetFloat(grabOffsetKey, grabOffsetY);
        storage->SetFloat(containerMinKey, regionMin.y - position);
    }

    if (ImGui::IsItemActive())
    {
        const float mouseY = ImGui::GetMousePos().y;
        const float grabOffsetY = storage->GetFloat(grabOffsetKey, 0.0f);
        const float containerMinY = storage->GetFloat(containerMinKey, regionMin.y - position);

        float newPos = mouseY - grabOffsetY - containerMinY;
        const float minP = m_minPixels;
        const float maxP = std::max(regionSize.y - m_minPixels, m_minPixels);
        newPos = std::clamp(newPos, minP, maxP);
        position = newPos;

        return true;
    }

    return false;
}

namespace ImGui
{
    namespace Utillity
    {
        bool VerticalSplitter(const char* id, float& ratio, ImVec2 availSpace,
            const float minRatio, const float maxRatio, float thickness)
        {
            // Convert ratio to pixel position within availSpace.x
            const float width = std::max(availSpace.x, 1.0f);
            float position = ratio * width;

            // regionMin = current cursor screen pos
            const ImVec2 regionMin = ImGui::GetCursorScreenPos();
            const ImVec2 regionSize = ImVec2(width, availSpace.y);

            const bool active = ::VerticalSplitter(id, position, regionMin, regionSize, thickness);

            // convert back to ratio
            ratio = std::clamp(position / width, minRatio, maxRatio);

            return active;
        }
    }
}
