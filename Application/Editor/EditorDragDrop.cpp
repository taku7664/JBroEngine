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

	bool accepted = false;
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ASSET_PAYLOAD_TYPE, flags))
	{
		if (payload->DataSize == sizeof(AssetPayload))
		{
			outPayload = *static_cast<const AssetPayload*>(payload->Data);
			accepted = true;
		}
	}

	ImGui::EndDragDropTarget();
	return accepted;
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
