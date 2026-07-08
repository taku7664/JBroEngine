#include "pch.h"
#include "ImAssetField.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/EditorDragDrop.h"
#include "Editor/ImItem/ImItemLocalizationKeys.h"
#include "Editor/ImItem/ImItemTypes.h"
#include "Editor/ImItem/ImReferenceField.h"

#include <sstream>

namespace
{
	std::string AssetPathLabel(const File::Path& path)
	{
		return path.filename().generic_string();
	}

	bool TryReadAssetPayload(const ImGuiPayload* payload, const EditorDragDrop::AssetPayload*& outPayload)
	{
		outPayload = nullptr;
		if (nullptr == payload
			|| false == payload->IsDataType(EditorDragDrop::ASSET_PAYLOAD_TYPE)
			|| payload->DataSize != sizeof(EditorDragDrop::AssetPayload))
		{
			return false;
		}

		outPayload = static_cast<const EditorDragDrop::AssetPayload*>(payload->Data);
		return nullptr != outPayload;
	}
}

ImAssetField::ImAssetField(const char* id, AssetGuid& guid)
	: m_id(id)
	, m_guid(guid)
{
}

ImAssetField& ImAssetField::Type(EAssetType type)
{
	m_type = type;
	return *this;
}

ImAssetField& ImAssetField::Width(float width)
{
	m_width = width;
	return *this;
}

ImAssetField& ImAssetField::AllowClear(bool allowClear)
{
	m_allowClear = allowClear;
	return *this;
}

ImAssetField& ImAssetField::AllowDirectories(bool allowDirectories)
{
	m_allowDirectories = allowDirectories;
	return *this;
}

ImAssetField& ImAssetField::NoneText(const char* text)
{
	m_noneText = text;
	return *this;
}

ImAssetField& ImAssetField::MissingText(const char* text)
{
	m_missingText = text;
	return *this;
}

ImAssetField& ImAssetField::Tooltip(const char* text)
{
	m_tooltip = text;
	return *this;
}

bool ImAssetField::Draw() const
{
	return ImReferenceField(
		nullptr != m_id ? m_id : "##asset_field",
		BuildDisplayLabel(m_guid, m_noneText, m_missingText),
		m_guid.IsNull())
		.Width(m_width)
		.AllowClear(m_allowClear)
		.Tooltip(BuildTooltip())
		.ClearTooltip(Loc::Text(ImItemLocKeys::CommonRemove))
		.OnAcceptDrop([this]() { return AcceptDrop(); })
		.OnClear([this]() { m_guid = INVALID_ASSET_GUID; })
		.Draw();
}

std::string ImAssetField::BuildDisplayLabel(const AssetGuid& guid, const char* noneText, const char* missingText)
{
	if (guid.IsNull())
	{
		return nullptr != noneText ? noneText : Loc::Text(ImItemLocKeys::InspectorRefNone);
	}

	const File::Path path = File::ResolvePath(guid);
	if (false == path.IsNull())
	{
		return AssetPathLabel(path);
	}

	std::ostringstream label;
	label << (nullptr != missingText ? missingText : Loc::Text(ImItemLocKeys::InspectorRefMissing))
		<< " ("
		<< guid.generic_string()
		<< ")";
	return label.str();
}

bool ImAssetField::AcceptDrop() const
{
	if (false == ImGui::BeginDragDropTarget())
	{
		return false;
	}

	bool changed = false;
	const EditorDragDrop::AssetPayload* hoveringPayload = nullptr;
	const bool compatible =
		TryReadAssetPayload(ImGui::GetDragDropPayload(), hoveringPayload)
		&& (m_allowDirectories || false == hoveringPayload->IsDirectory)
		&& IsExpectedType(hoveringPayload->Type);

	if (compatible)
	{
		if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_PAYLOAD_TYPE))
		{
			const EditorDragDrop::AssetPayload* payload = nullptr;
			if (TryReadAssetPayload(accepted, payload))
			{
				m_guid = EditorDragDrop::GetGuid(*payload);
				changed = true;
			}
		}
	}

	ImGui::EndDragDropTarget();
	return changed;
}

bool ImAssetField::IsExpectedType(EAssetType type) const
{
	return EAssetType::Unknown == m_type || type == m_type;
}

std::string ImAssetField::BuildTooltip() const
{
	if (false == ImItem::IsEmptyText(m_tooltip))
	{
		return m_tooltip;
	}

	if (m_guid.IsNull())
	{
		return {};
	}

	const File::Path path = File::ResolvePath(m_guid);
	return false == path.IsNull()
		? path.generic_string()
		: m_guid.generic_string();
}

#endif
