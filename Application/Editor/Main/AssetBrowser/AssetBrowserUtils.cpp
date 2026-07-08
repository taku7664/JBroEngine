#include "pch.h"
#include "AssetBrowserUtils.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Localization/EditorLocalizationKeys.h"
#include "Core/Localization/LocalizationManager.h"

std::string AssetBrowserUtils::ToLowerAscii(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return text;
}

std::time_t AssetBrowserUtils::FileTimeToTimeT(std::filesystem::file_time_type fileTime)
{
	const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
		fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
	return std::chrono::system_clock::to_time_t(systemTime);
}

const char* AssetBrowserUtils::GetAssetTypeName(EAssetType type)
{
	switch (type)
	{
	case EAssetType::Sprite: return Loc::Text(EditorLocKeys::AssetTypeSprite);
	case EAssetType::Mesh: return Loc::Text(EditorLocKeys::AssetTypeMesh);
	case EAssetType::Material: return Loc::Text(EditorLocKeys::AssetTypeMaterial);
	case EAssetType::Shader: return Loc::Text(EditorLocKeys::AssetTypeShader);
	case EAssetType::Scene: return Loc::Text(EditorLocKeys::AssetTypeScene);
	case EAssetType::Prefab: return Loc::Text(EditorLocKeys::AssetTypePrefab);
	case EAssetType::Script: return Loc::Text(EditorLocKeys::AssetTypeScript);
	case EAssetType::Audio: return Loc::Text(EditorLocKeys::AssetTypeAudio);
	case EAssetType::AudioEffect: return Loc::Text(EditorLocKeys::AssetTypeAudioEffect);
	case EAssetType::FontFace: return Loc::Text(EditorLocKeys::AssetTypeFontFace);
	case EAssetType::FontFamily: return Loc::Text(EditorLocKeys::AssetTypeFontFamily);
	case EAssetType::Custom: return Loc::Text(EditorLocKeys::AssetTypeCustom);
	default: return Loc::Text(EditorLocKeys::AssetTypeUnknown);
	}
}

#endif
