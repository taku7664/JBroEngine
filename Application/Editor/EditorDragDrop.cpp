#include "pch.h"
#include "EditorDragDrop.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

namespace
{
	void CopyText(char* destination, std::size_t destinationSize, const std::string& source)
	{
		if (nullptr == destination || 0 == destinationSize)
		{
			return;
		}

		const std::size_t copySize = std::min(destinationSize - 1, source.size());
		std::memcpy(destination, source.data(), copySize);
		destination[copySize] = '\0';
	}

	// 아이템이 아니라 현재 창 사각형 전체를 타깃으로 연다(배경 드롭용).
	bool BeginCurrentWindowDragDropTarget()
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		return nullptr != window && ImGui::BeginDragDropTargetCustom(window->Rect(), window->ID);
	}

	// Begin*DragDropTarget 이 열린 뒤의 공통 수신부(아이템 타깃 / 창 타깃이 공유).
	bool AcceptAssetPayloadInOpenTarget(EditorDragDrop::AssetPayload& outPayload, ImGuiDragDropFlags flags)
	{
		bool accepted = false;
		if (const ImGuiPayload* payload =
			ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_PAYLOAD_TYPE, flags))
		{
			if (payload->DataSize == sizeof(EditorDragDrop::AssetPayload))
			{
				outPayload = *static_cast<const EditorDragDrop::AssetPayload*>(payload->Data);
				accepted = true;
			}
		}
		ImGui::EndDragDropTarget();
		return accepted;
	}

	bool AcceptLayerPayloadInOpenTarget(CGameLayer*& outLayer, ImGuiDragDropFlags flags)
	{
		bool accepted = false;
		if (const ImGuiPayload* payload =
			ImGui::AcceptDragDropPayload(EditorDragDrop::HIERARCHY_LAYER_PAYLOAD, flags))
		{
			if (payload->DataSize == sizeof(CGameLayer*))
			{
				outLayer = *static_cast<CGameLayer* const*>(payload->Data);
				accepted = nullptr != outLayer;
			}
		}
		ImGui::EndDragDropTarget();
		return accepted;
	}
}

bool EditorDragDrop::BeginAssetDragDropSource(const AssetPayloadDesc& desc)
{
	if (false == ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
	{
		return false;
	}

	AssetPayload payload;
	CopyText(payload.Guid, MAX_GUID_TEXT_LENGTH, desc.Guid.generic_string());
	CopyText(payload.RelativePath, MAX_PATH_TEXT_LENGTH, desc.RelativePath.generic_string());
	payload.Type = desc.Type;
	payload.IsDirectory = desc.IsDirectory;

	ImGui::SetDragDropPayload(ASSET_PAYLOAD_TYPE, &payload, sizeof(payload));

	const char* label = nullptr != desc.PreviewLabel ? desc.PreviewLabel : payload.RelativePath;

	if (0 != desc.PreviewTextureID && desc.PreviewSize > 0.0f)
	{
		// 아이콘 모드와 동일하게: 아이콘 위, 이름 아래 가운데 정렬.
		const ImVec2 imgSize(desc.PreviewSize, desc.PreviewSize);
		const ImVec2 textSize = ImGui::CalcTextSize(label);
		const float  cellW    = std::max(imgSize.x, textSize.x);

		ImGui::BeginGroup();
		{
			const float imgPadX = (cellW - imgSize.x) * 0.5f;
			if (imgPadX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + imgPadX);
			ImGui::Image(desc.PreviewTextureID, imgSize);

			const float txtPadX = (cellW - textSize.x) * 0.5f;
			if (txtPadX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + txtPadX);
			ImGui::TextUnformatted(label);
		}
		ImGui::EndGroup();
	}
	else
	{
		ImGui::TextUnformatted(label);
	}

	ImGui::EndDragDropSource();
	return true;
}

bool EditorDragDrop::AcceptAssetDragDropPayload(AssetPayload& outPayload, ImGuiDragDropFlags flags)
{
	if (false == ImGui::BeginDragDropTarget())
	{
		return false;
	}
	return AcceptAssetPayloadInOpenTarget(outPayload, flags);
}

bool EditorDragDrop::AcceptLayerDragDropPayload(CGameLayer*& outLayer, ImGuiDragDropFlags flags)
{
	if (false == ImGui::BeginDragDropTarget())
	{
		return false;
	}
	return AcceptLayerPayloadInOpenTarget(outLayer, flags);
}

bool EditorDragDrop::AcceptLayerDragDropPayloadOnWindow(CGameLayer*& outLayer, ImGuiDragDropFlags flags)
{
	if (false == BeginCurrentWindowDragDropTarget())
	{
		return false;
	}
	return AcceptLayerPayloadInOpenTarget(outLayer, flags);
}

bool EditorDragDrop::AcceptAssetDragDropPayloadOnWindow(AssetPayload& outPayload, ImGuiDragDropFlags flags)
{
	if (false == BeginCurrentWindowDragDropTarget())
	{
		return false;
	}
	return AcceptAssetPayloadInOpenTarget(outPayload, flags);
}

File::Guid EditorDragDrop::GetGuid(const AssetPayload& payload)
{
	return File::Guid(payload.Guid);
}

File::Path EditorDragDrop::GetRelativePath(const AssetPayload& payload)
{
	return File::Path(payload.RelativePath);
}

#endif
