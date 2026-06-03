#pragma once

#include "Core/Asset/AssetTypes.h"

#include <string>
#include <vector>

struct ProjectLoadDesc
{
	File::Path ProjectFilePath;
};

enum class EScriptBuildConfiguration
{
	Debug,
	Release
};

enum class EBuildTargetPlatform
{
	Windows,
	Web,
	Android,
	IOS
};

enum class EBuildConfiguration
{
	Debug,
	Release
};

enum class EBuildScriptMode
{
	DynamicLibrary,
	Static
};

struct ProjectBuildSettings
{
	std::string ProductName;
	EBuildTargetPlatform TargetPlatform = EBuildTargetPlatform::Windows;
	EBuildConfiguration BuildConfiguration = EBuildConfiguration::Release;
	std::string OutputDirectory = "Dist/Games";
	std::string StartupScene;
	std::vector<std::string> BuildScenes;
	EBuildScriptMode ScriptMode = EBuildScriptMode::DynamicLibrary;
	std::string ScriptProjectPath = "Contents/GameScript.vcxproj";
	EScriptBuildConfiguration ScriptBuildConfiguration = EScriptBuildConfiguration::Release;
	std::string ScriptOutputLibraryPath = "GameScript.dll";
	AssetGuid WindowsIconGuid = INVALID_ASSET_GUID;
	std::string AndroidApplicationId = "com.jbro.game";
	std::uint32_t AndroidMinSdkVersion = 26;
	std::uint32_t AndroidTargetSdkVersion = 35;
	std::string AndroidAbi = "arm64-v8a";
	std::string IOSBundleIdentifier = "com.jbro.game";
	std::string IOSTeamId;
	std::string IOSMinimumOSVersion = "15.0";
};

struct ProjectInfo
{
	std::uint32_t Version = 1;
	File::Path OriginPath;
	File::Path ProjectFilePath;
	File::Path RootPath;
	File::Path ContentPath;
	File::Path AssetPath;
	File::Path ScriptPath;

	// 게임 렌더 타겟 해상도 (픽셀 단위)
	std::uint32_t ResolutionWidth  = 1920;
	std::uint32_t ResolutionHeight = 1080;

	// 씬 뷰 에디터 카메라 (저장/복원용)
	float SceneViewCamX    = 0.0f;
	float SceneViewCamY    = 0.0f;
	float SceneViewCamSize = 5.0f;

	// 좌표계 단위: 1 월드 유닛 = PixelsPerUnit 픽셀
	float PixelsPerUnit = 100.0f;

	// 스크립트 DLL 경로 (legacy manual loader, 프로젝트 루트 기준 상대경로 또는 절대경로)
	std::string ScriptDllPath;

	// Windows Editor 전용 Script build/reload 설정
	std::string ScriptSourceDirectory = "Contents";
	std::string ScriptBuildCommand;
	std::string ScriptOutputLibraryPath = "x64/Debug/GameScript.dll";
	std::string ScriptIntermediateDirectory = "Build/Intermediate/LiveCompile";
	EScriptBuildConfiguration ScriptBuildConfiguration = EScriptBuildConfiguration::Debug;
	bool ScriptAutoRebuildEnabled = true;

	// 마지막으로 열었던 씬 경로 (Assets 폴더 기준 상대경로)
	std::string LastOpenedScenePath;

	// 게임 빌드본 생성 설정. 에디터 상태/LiveCompile 설정과 분리한다.
	ProjectBuildSettings BuildSettings;

	// 프로젝트별 에디터 상태
	std::string EditorLocaleCode = "ko-KR";
	std::string ImGuiIniSettings;

	// 자산 워처 무시 패턴 — glob (* / ?) 한 줄당 하나의 패턴.
	// 패턴은 Assets 폴더 기준 상대경로 또는 파일명에 매칭된다.
	// 외부 도구 임시 파일(*.tmp, ~$*, *.swp 등) 을 자산 import 시도에서 거른다.
	// 사용자는 ProjectSettings 의 "Asset Watcher" 카테고리에서 직접 편집 가능.
	std::vector<std::string> AssetWatchIgnorePatterns = {
		"*.tmp",
		"*.swp",
		"~$*",
		".DS_Store",
		"Thumbs.db",
	};
};
