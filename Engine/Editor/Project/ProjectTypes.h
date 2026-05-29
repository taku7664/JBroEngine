#pragma once

#include <string>

struct ProjectLoadDesc
{
	File::Path ProjectFilePath;
};

enum class EScriptBuildConfiguration
{
	Debug,
	Release
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

	// 프로젝트별 에디터 상태
	std::string EditorLocaleCode = "ko-KR";
	std::string ImGuiIniSettings;
};

