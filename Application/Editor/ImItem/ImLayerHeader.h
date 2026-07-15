#pragma once

#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h" // ImRect

// 레이어 행 — CollapsingHeader 처럼 접히되 왼쪽에 레이어 썸네일이 붙고, 행 높이가 썸네일에
// 맞춰 커진다(ImTree 는 한 줄 높이 고정이라 썸네일을 못 담는다).
//
// 썸네일·라벨은 ImGui 아이템을 만들지 않고 DrawList 로 직접 그린다. 호출자가 행 바로 뒤에서
// IsItem*/BeginDragDropSource/BeginPopupContextItem 을 쓰려면 "마지막 아이템" 이 트리 노드로
// 남아 있어야 하기 때문이다(아이템을 하나라도 더 그리면 그쪽으로 넘어간다).
// 가시성 토글 같은 버튼은 호출자가 행 처리(선택/드래그/드롭/컨텍스트) 뒤에 SameLine 으로 얹는다.
struct ImLayerHeaderDesc
{
	const char* Label = nullptr;
	ImTextureID Thumbnail = 0;                      // 0 = 아직 없음(자리와 테두리만)
	ImVec2      ThumbnailSize = ImVec2(0.0f, 0.0f); // (0,0) = 썸네일 영역 자체를 두지 않음
	bool        Selected = false;
	bool        DefaultOpen = true;
	// 행 오른쪽에서 라벨을 그리지 않고 비워둘 폭(px). 호출자가 나중에 얹을 버튼 자리.
	float       ReservedRightWidth = 0.0f;
};

struct ImLayerHeaderResult
{
	bool   IsOpen = false;
	ImRect RowRect;              // 행 전체(드롭 하이라이트/히트 영역용)
	float  RowHeight = 0.0f;
};

ImLayerHeaderResult ImLayerHeader(const char* id, const ImLayerHeaderDesc& desc);
// IsOpen 일 때 자식 렌더가 끝난 뒤 호출(TreePop).
void ImLayerHeaderEnd();
