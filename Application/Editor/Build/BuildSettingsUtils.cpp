#include "pch.h"
#include "BuildSettingsUtils.h"

#include "Utillity/String/StringUtillity.h"

#include <algorithm>
#include <cctype>
#include <cwctype>

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

namespace
{
	bool IsAsciiAlpha(char value)
	{
		return 0 != std::isalpha(static_cast<unsigned char>(value));
	}

	bool IsAsciiAlnum(char value)
	{
		return 0 != std::isalnum(static_cast<unsigned char>(value));
	}

	bool IsAndroidIdentifierSegment(const std::string& segment)
	{
		return false == segment.empty()
			&& IsAsciiAlpha(segment.front())
			&& std::all_of(segment.begin() + 1, segment.end(), [](char value) {
				return IsAsciiAlnum(value) || value == '_';
			});
	}

	bool IsBundleIdentifierSegment(const std::string& segment)
	{
		return false == segment.empty()
			&& IsAsciiAlnum(segment.front())
			&& std::all_of(segment.begin() + 1, segment.end(), [](char value) {
				return IsAsciiAlnum(value) || value == '-';
			});
	}

	template <typename SegmentValidator>
	bool IsDottedIdentifier(const std::string& value, SegmentValidator&& isValidSegment)
	{
		if (BuildSettingsUtils::IsBlank(value))
		{
			return false;
		}

		std::size_t segmentCount = 0;
		std::size_t start = 0;
		while (start <= value.size())
		{
			const std::size_t end = value.find('.', start);
			const std::string segment = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
			if (false == isValidSegment(segment))
			{
				return false;
			}
			++segmentCount;
			if (end == std::string::npos)
			{
				break;
			}
			start = end + 1;
		}
		return segmentCount >= 2;
	}
}

const char* BuildSettingsUtils::GetTargetPlatformName(EBuildTargetPlatform platform)
{
	switch (platform)
	{
	case EBuildTargetPlatform::Web: return "Web";
	case EBuildTargetPlatform::Android: return "Android";
	case EBuildTargetPlatform::IOS: return "IOS";
	case EBuildTargetPlatform::Windows:
	default: return "Windows";
	}
}

const char* BuildSettingsUtils::GetBuildConfigurationName(EBuildConfiguration configuration)
{
	return EBuildConfiguration::Debug == configuration ? "Debug" : "Release";
}

const char* BuildSettingsUtils::GetEngineBuildConfigurationName(EBuildConfiguration configuration)
{
	return EBuildConfiguration::Debug == configuration ? "Debug_Game" : "Release_Game";
}

const char* BuildSettingsUtils::GetPlatformLabelKey(EBuildTargetPlatform platform)
{
	switch (platform)
	{
	case EBuildTargetPlatform::Web: return EditorLocKeys::BuildSettingsCategoryWeb;
	case EBuildTargetPlatform::Android: return EditorLocKeys::BuildSettingsCategoryAndroid;
	case EBuildTargetPlatform::IOS: return EditorLocKeys::BuildSettingsCategoryIos;
	case EBuildTargetPlatform::Windows:
	default: return EditorLocKeys::BuildSettingsCategoryWindows;
	}
}

bool BuildSettingsUtils::IsBlank(const std::string& value)
{
	return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

bool BuildSettingsUtils::IsValidAndroidApplicationId(const std::string& value)
{
	return IsDottedIdentifier(value, IsAndroidIdentifierSegment);
}

bool BuildSettingsUtils::IsValidIOSBundleIdentifier(const std::string& value)
{
	return IsDottedIdentifier(value, IsBundleIdentifierSegment);
}

bool BuildSettingsUtils::IsValidVersionString(const std::string& value)
{
	if (IsBlank(value))
	{
		return false;
	}

	bool hasDigitInSegment = false;
	for (char character : value)
	{
		if (std::isdigit(static_cast<unsigned char>(character)))
		{
			hasDigitInSegment = true;
			continue;
		}
		if (character == '.' && hasDigitInSegment)
		{
			hasDigitInSegment = false;
			continue;
		}
		return false;
	}
	return hasDigitInSegment;
}

bool BuildSettingsUtils::HasCanvasFileExtension(const std::string& value)
{
	if (IsBlank(value))
	{
		return false;
	}

	std::wstring extension = File::Path(Utillity::U8ToWString(value)).extension().wstring();
	std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t character) {
		return static_cast<wchar_t>(std::towlower(character));
	});
	return extension == L".jcanvas";
}

bool BuildSettingsUtils::HasRequiredIssueForPlatform(
	const ProjectBuildSettings& settings,
	EBuildTargetPlatform platform)
{
	if (IsBlank(settings.ProductName)
		|| IsBlank(settings.OutputDirectory)
		|| false == HasCanvasFileExtension(settings.StartupCanvas))
	{
		return true;
	}

	if (EBuildTargetPlatform::Android == platform)
	{
		return false == IsValidAndroidApplicationId(settings.AndroidApplicationId)
			|| 0 == settings.AndroidMinSdkVersion
			|| 0 == settings.AndroidTargetSdkVersion
			|| settings.AndroidTargetSdkVersion < settings.AndroidMinSdkVersion;
	}
	if (EBuildTargetPlatform::IOS == platform)
	{
		return false == IsValidIOSBundleIdentifier(settings.IOSBundleIdentifier)
			|| false == IsValidVersionString(settings.IOSMinimumOSVersion);
	}
	return false;
}

std::vector<EBuildTargetPlatform> BuildSettingsUtils::GetEnabledPlatforms(const ProjectBuildSettings& settings)
{
	std::vector<EBuildTargetPlatform> platforms;
	if (settings.EnableWindows) platforms.push_back(EBuildTargetPlatform::Windows);
	if (settings.EnableWeb) platforms.push_back(EBuildTargetPlatform::Web);
	if (settings.EnableAndroid) platforms.push_back(EBuildTargetPlatform::Android);
	if (settings.EnableIOS) platforms.push_back(EBuildTargetPlatform::IOS);
	return platforms;
}

#endif
