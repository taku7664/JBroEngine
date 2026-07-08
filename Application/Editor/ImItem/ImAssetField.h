#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Core/Asset/AssetTypes.h"

#include <string>

class ImAssetField
{
public:
	ImAssetField(const char* id, AssetGuid& guid);

	ImAssetField& Type(EAssetType type);
	ImAssetField& Width(float width);
	ImAssetField& AllowClear(bool allowClear = true);
	ImAssetField& AllowDirectories(bool allowDirectories = true);
	ImAssetField& NoneText(const char* text);
	ImAssetField& MissingText(const char* text);
	ImAssetField& Tooltip(const char* text);

	bool Draw() const;
	bool operator()() const { return Draw(); }

	static std::string BuildDisplayLabel(
		const AssetGuid& guid,
		const char* noneText = nullptr,
		const char* missingText = nullptr);

	struct DropPayloadView
	{
		AssetGuid Guid = INVALID_ASSET_GUID;
		File::Path RelativePath = File::NULL_PATH;
		EAssetType DeclaredType = EAssetType::Unknown;
		EAssetType ResolvedType = EAssetType::Unknown;
		bool IsDirectory = false;
		bool IsValid = false;
	};

private:
	bool AcceptDrop() const;
	bool IsPayloadCompatible(const DropPayloadView& payload) const;
	void DrawRejectedDropFeedback(const DropPayloadView& payload) const;
	std::string BuildRejectedDropTooltip(const DropPayloadView& payload) const;
	std::string BuildTooltip() const;

	const char* m_id = nullptr;
	AssetGuid& m_guid;
	EAssetType m_type = EAssetType::Unknown;
	float m_width = 0.0f;
	bool m_allowClear = true;
	bool m_allowDirectories = false;
	const char* m_noneText = nullptr;
	const char* m_missingText = nullptr;
	const char* m_tooltip = nullptr;
};

#endif
