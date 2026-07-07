#include "pch.h"
#include "AssetBrowserUtils.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

std::string AssetBrowserUtils::PathToUtf8(const std::filesystem::path& path)
{
	const auto text = path.generic_u8string();
	return std::string(reinterpret_cast<const char*>(text.c_str()), text.size());
}

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
	case EAssetType::Sprite: return "Sprite";
	case EAssetType::Mesh: return "Mesh";
	case EAssetType::Material: return "Material";
	case EAssetType::Shader: return "Shader";
	case EAssetType::Scene: return "Scene";
	case EAssetType::Prefab: return "Prefab";
	case EAssetType::Script: return "Script";
	case EAssetType::Audio: return "Audio";
	case EAssetType::FontFace: return "FontFace";
	case EAssetType::FontFamily: return "FontFamily";
	case EAssetType::Custom: return "Custom";
	default: return "Unknown";
	}
}

#endif
