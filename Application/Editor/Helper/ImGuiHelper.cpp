#include "pch.h"
#include "ImGuiHelper.h"

void ImGuiHelper::ApplyCompactImGuiStyle(float scale)
{

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);    // 패딩, 갭, 라운딩 등 일괄 축소
}
