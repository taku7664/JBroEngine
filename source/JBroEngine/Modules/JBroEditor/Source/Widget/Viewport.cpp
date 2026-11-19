#include <JBro/Editor/Widget/Viewport.h>

#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/EditorUI.h>
#include <JBro/Editor/Widget/Button.h>

#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro::Widget
{
    ViewportRect ViewportImage(TextureHandle texture, Extent2D extent)
    {
        ViewportRect rect;
        const ImVec2 panel = ImGui::GetContentRegionAvail();
        if (panel.x <= 0.0f || panel.y <= 0.0f)
        {
            return rect;
        }
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        rect.hasPanel = true;
        rect.panelLeft = origin.x;
        rect.panelTop = origin.y;
        rect.panelWidth = panel.x;
        rect.panelHeight = panel.y;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        // 그림이 붙지 않는 자리의 바탕이다. 레터박스가 창 배경과 같은 색이면
        // 그림이 어디까지인지 보이지 않는다.
        draw->AddRectFilled(origin, ImVec2(origin.x + panel.x, origin.y + panel.y),
            ImGui::GetColorU32(EditorTheme::ViewportBackground));

        if (texture.IsValid() && extent.width != 0 && extent.height != 0)
        {
            // **비율을 지켜 패널 안에 맞춘다(레터박스).** 늘려 붙이면 에디터 창 모양에
            // 따라 그림이 찌그러져 보인다.
            const Float viewAspect = static_cast<JBro::Float>(extent.width) / static_cast<JBro::Float>(extent.height);
            const Float panelAspect = panel.x / panel.y;
            ImVec2 size = panel;
            if (viewAspect > panelAspect)
            {
                size.y = panel.x / viewAspect;
            }
            else
            {
                size.x = panel.y * viewAspect;
            }
            const ImVec2 imageMin(origin.x + (panel.x - size.x) * 0.5f, origin.y + (panel.y - size.y) * 0.5f);
            rect.hasImage = true;
            rect.imageLeft = imageMin.x;
            rect.imageTop = imageMin.y;
            rect.imageWidth = size.x;
            rect.imageHeight = size.y;
            draw->AddImage(static_cast<ImTextureID>(EditorUI::ToTextureId(texture)), imageMin,
                ImVec2(imageMin.x + size.x, imageMin.y + size.y));
        }
        // 자리를 차지해 두어야 스크롤과 다음 줄이 어긋나지 않는다.
        ImGui::Dummy(panel);
        return rect;
    }

    void ViewportStatusLine(const ViewportRect& rect, UInt32 line, const char* icon, ImU32 color, const char* text)
    {
        if (false == rect.hasPanel || text == nullptr)
        {
            return;
        }
        const Float lineHeight = ImGui::GetTextLineHeight();
        const Float gap = ImGui::GetStyle().ItemInnerSpacing.x;
        const Float y = rect.panelTop + 10.0f + 18.0f * static_cast<JBro::Float>(line);
        const ImVec2 iconMin(rect.panelLeft + 12.0f, y);
        if (icon != nullptr)
        {
            DrawGlyphCentered(icon, iconMin, ImVec2(iconMin.x + lineHeight, iconMin.y + lineHeight), color);
        }
        ImGui::GetWindowDrawList()->AddText(ImVec2(iconMin.x + lineHeight + gap, y), color, text);
    }
}
