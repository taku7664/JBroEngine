#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Editor/Project/ProjectTypes.h"

#include <string>
#include <vector>

namespace BuildSettingsUtils
{
	const char* GetTargetPlatformName(EBuildTargetPlatform platform);
	const char* GetBuildConfigurationName(EBuildConfiguration configuration);
	const char* GetEngineBuildConfigurationName(EBuildConfiguration configuration);
	const char* GetPlatformLabelKey(EBuildTargetPlatform platform);

	bool IsBlank(const std::string& value);
	bool IsValidAndroidApplicationId(const std::string& value);
	bool IsValidIOSBundleIdentifier(const std::string& value);
	bool IsValidVersionString(const std::string& value);
	bool HasSceneFileExtension(const std::string& value);
	bool HasRequiredIssueForPlatform(const ProjectBuildSettings& settings, EBuildTargetPlatform platform);
	std::vector<EBuildTargetPlatform> GetEnabledPlatforms(const ProjectBuildSettings& settings);
}

#endif
