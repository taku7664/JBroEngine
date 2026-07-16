#include "pch.h"
#include "ImAssetField.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/EditorDragDrop.h"
#include "Editor/ImItem/ImItemLocalizationKeys.h"
#include "Editor/ImItem/ImItemTypes.h"
#include "Editor/ImItem/ImReferenceField.h"
#include "Editor/Localization/EditorLocalizationKeys.h"
#include "Core/Localization/LocalizationManager.h"
#include "Engine/Core/Asset/AssetTypeRules.h"

#include <sstream>

namespace
{
	std::string AssetPathLabel(const File::Path& path)
	{
		return path.filename().generic_string();
	}

	const char* GetLocalizedAssetTypeName(EAssetType type)
	{
		switch (type)
		{
		case EAssetType::Sprite:
			return Loc::Text(EditorLocKeys::AssetTypeSprite);
		case EAssetType::Mesh:
			return Loc::Text(EditorLocKeys::AssetTypeMesh);
		case EAssetType::Material:
			return Loc::Text(EditorLocKeys::AssetTypeMaterial);
		case EAssetType::Shader:
			return Loc::Text(EditorLocKeys::AssetTypeShader);
		case EAssetType::Scene:
			return Loc::Text(EditorLocKeys::AssetTypeScene);
		case EAssetType::Prefab:
			return Loc::Text(EditorLocKeys::AssetTypePrefab);
		case EAssetType::Script:
			return Loc::Text(EditorLocKeys::AssetTypeScript);
		case EAssetType::Audio:
			return Loc::Text(EditorLocKeys::AssetTypeAudio);
		case EAssetType::AudioEffect:
			return Loc::Text(EditorLocKeys::AssetTypeAudioEffect);
		case EAssetType::FontFace:
			return Loc::Text(EditorLocKeys::AssetTypeFontFace);
		case EAssetType::FontFamily:
			return Loc::Text(EditorLocKeys::AssetTypeFontFamily);
		case EAssetType::Layer:
			return Loc::Text(EditorLocKeys::AssetTypeLayer);
		case EAssetType::Custom:
			return Loc::Text(EditorLocKeys::AssetTypeCustom);
		case EAssetType::Unknown:
		default:
			return Loc::Text(EditorLocKeys::AssetTypeUnknown);
		}
	}

	bool TryReadAssetPayload(const ImGuiPayload* payload, ImAssetField::DropPayloadView& outPayload)
	{
		outPayload = {};
		if (nullptr == payload
			|| false == payload->IsDataType(EditorDragDrop::ASSET_PAYLOAD_TYPE)
			|| payload->DataSize != sizeof(EditorDragDrop::AssetPayload))
		{
			return false;
		}

		const EditorDragDrop::AssetPayload* assetPayload = static_cast<const EditorDragDrop::AssetPayload*>(payload->Data);
		if (nullptr == assetPayload)
		{
			outPayload = {};
			return false;
		}

		outPayload.Guid = EditorDragDrop::GetGuid(*assetPayload);
		outPayload.RelativePath = EditorDragDrop::GetRelativePath(*assetPayload);
		outPayload.DeclaredType = assetPayload->Type;
		outPayload.ResolvedType = CAssetTypeRules::ResolveType(assetPayload->Type, outPayload.RelativePath);
		outPayload.IsDirectory = assetPayload->IsDirectory;
		outPayload.IsValid = true;
		return true;
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
	DropPayloadView hoveringPayload;
	const bool hasPayload = TryReadAssetPayload(ImGui::GetDragDropPayload(), hoveringPayload);
	const bool compatible = hasPayload && IsPayloadCompatible(hoveringPayload);

	if (compatible)
	{
		if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_PAYLOAD_TYPE))
		{
			DropPayloadView payload;
			if (TryReadAssetPayload(accepted, payload))
			{
				m_guid = payload.Guid;
				changed = true;
			}
		}
	}
	else if (hasPayload)
	{
		DrawRejectedDropFeedback(hoveringPayload);
	}

	ImGui::EndDragDropTarget();
	return changed;
}

bool ImAssetField::IsPayloadCompatible(const DropPayloadView& payload) const
{
	if (false == payload.IsValid)
	{
		return false;
	}
	if (payload.IsDirectory)
	{
		return m_allowDirectories;
	}
	return CAssetTypeRules::IsAssignableTo(m_type, payload.DeclaredType, payload.RelativePath);
}

void ImAssetField::DrawRejectedDropFeedback(const DropPayloadView& payload) const
{
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	if (nullptr != drawList)
	{
		const ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.95f, 0.20f, 0.20f, 1.0f));
		const ImU32 fillColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.95f, 0.05f, 0.05f, 0.12f));
		drawList->AddRectFilled(min, max, fillColor, ImGui::GetStyle().FrameRounding);
		drawList->AddRect(min, max, borderColor, ImGui::GetStyle().FrameRounding, 0, 2.0f);
	}

	if (ImGui::IsMouseHoveringRect(min, max))
	{
		const std::string tooltip = BuildRejectedDropTooltip(payload);
		ImGui::BeginTooltip();
		ImGui::TextWrapped("%s", tooltip.c_str());
		ImGui::EndTooltip();
	}
}

std::string ImAssetField::BuildRejectedDropTooltip(const DropPayloadView& payload) const
{
	std::ostringstream text;
	text << Loc::Text(ImItemLocKeys::AssetDropRejected);

	if (payload.IsDirectory && false == m_allowDirectories)
	{
		text << '\n' << Loc::Text(ImItemLocKeys::AssetDropDirectoriesNotAllowed);
		return text.str();
	}

	if (EAssetType::Unknown != m_type)
	{
		text << '\n'
			<< Loc::Text(ImItemLocKeys::AssetDropExpected)
			<< ' '
			<< GetLocalizedAssetTypeName(m_type);

		const std::string extensions = CAssetTypeRules::GetAllowedExtensionsText(m_type);
		if (false == extensions.empty())
		{
			text << " (" << extensions << ')';
		}
	}

	if (payload.IsValid)
	{
		text << '\n'
			<< Loc::Text(ImItemLocKeys::AssetDropDragged)
			<< ' '
			<< GetLocalizedAssetTypeName(payload.ResolvedType);

		if (false == payload.RelativePath.empty())
		{
			text << " - " << payload.RelativePath.filename().generic_string();
		}
	}
	return text.str();
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
