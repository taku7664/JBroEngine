#pragma once
#include "ThirdParty/imgui/imgui.h"
#include "Editor/ImItem/ImText.h"

bool ImTextButton(const char* label, const ImVec2& size = ImVec2(0, 0), const ImVec2& offset = ImVec2(0, 0), ImGuiButtonFlags flags = ImGuiButtonFlags_None);

// ImText 설정(scale/align)을 적용해 버튼 위에 라벨을 그린다. 스케일된 텍스트 크기를
// 버튼 rect 기준으로 정렬한다(세로 항상 중앙, 가로는 ImText.Align).
bool ImTextButton(ImText text, const char* label, const ImVec2& size = ImVec2(0, 0), const ImVec2& offset = ImVec2(0, 0), ImGuiButtonFlags flags = ImGuiButtonFlags_None);
