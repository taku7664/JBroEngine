#include "pch.h"
#include "ImLayerHeader.h"

#include "ImTree.h"

ImLayerHeaderResult ImLayerHeader(const char* id, const ImLayerHeaderDesc& desc)
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (desc.Selected)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (desc.DefaultOpen)
	{
		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}

	// 썸네일 높이만큼 행을 키운다. ImTreeEnd 는 커서만 되돌리므로 이후 SameLine 배치가 그대로 먹는다.
	ImTreeDrawContext context;
	const bool isOpen = ImTreeBegin(id, flags, &context, desc.ThumbnailSize.y);
	ImTreeEnd();

	ImLayerHeaderResult result;
	result.IsOpen = isOpen;
	result.RowRect = context.RowRect;
	result.RowHeight = context.RowRect.Max.y - context.RowRect.Min.y;

	if (false == context.IsVisible)
	{
		return result;
	}

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	float labelX = context.ContentRect.Min.x;

	if (desc.ThumbnailSize.x > 0.0f && desc.ThumbnailSize.y > 0.0f)
	{
		const ImVec2 thumbnailMin = context.ContentRect.Min;
		const ImVec2 thumbnailMax = ImVec2(
			thumbnailMin.x + desc.ThumbnailSize.x,
			thumbnailMin.y + desc.ThumbnailSize.y);

		if (0 != desc.Thumbnail)
		{
			drawList->AddImage(desc.Thumbnail, thumbnailMin, thumbnailMax);
		}
		drawList->AddRect(thumbnailMin, thumbnailMax, ImGui::GetColorU32(ImGuiCol_Border));
		labelX = thumbnailMax.x + ImGui::GetStyle().ItemInnerSpacing.x;
	}

	if (nullptr != desc.Label)
	{
		const ImVec2 labelSize = ImGui::CalcTextSize(desc.Label);
		const ImVec2 labelPos(labelX, result.RowRect.Min.y + (result.RowHeight - labelSize.y) * 0.5f);
		// 긴 이름이 오른쪽 버튼 자리를 침범하지 않게 잘라 그린다.
		const ImVec4 clipRect(
			labelPos.x,
			result.RowRect.Min.y,
			result.RowRect.Max.x - desc.ReservedRightWidth,
			result.RowRect.Max.y);
		drawList->AddText(
			nullptr, 0.0f, labelPos, ImGui::GetColorU32(ImGuiCol_Text), desc.Label, nullptr, 0.0f, &clipRect);
	}

	return result;
}

void ImLayerHeaderEnd()
{
	ImGui::TreePop();
}
