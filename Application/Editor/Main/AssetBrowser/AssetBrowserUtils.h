#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Core/Asset/AssetTypes.h"

#include <ctime>
#include <filesystem>
#include <string>

namespace AssetBrowserUtils
{
	std::string ToLowerAscii(std::string text);
	std::time_t FileTimeToTimeT(std::filesystem::file_time_type fileTime);
	const char* GetAssetTypeName(EAssetType type);
}

#endif
