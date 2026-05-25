#pragma once

#include "File/FilePath.h"
#include <string>

struct ProjectLoadDesc
{
	File::Path ProjectFilePath;
};

struct ProjectInfo
{
	std::uint32_t Version = 1;
	File::Path OriginPath;
	File::Path ProjectFilePath;
	File::Path RootPath;
	File::Path AssetPath;

	// 게임 렌더 타겟 해상도 (픽셀 단위)
	std::uint32_t ResolutionWidth  = 1920;
	std::uint32_t ResolutionHeight = 1080;

	// 씬 뷰 에디터 카메라 (저장/복원용)
	float SceneViewCamX    = 0.0f;
	float SceneViewCamY    = 0.0f;
	float SceneViewCamSize = 5.0f;

	// 스크립트 DLL 경로 (프로젝트 루트 기준 상대경로 또는 절대경로)
	std::string ScriptDllPath;

	// 마지막으로 열었던 씬 경로 (Assets 폴더 기준 상대경로)
	std::string LastOpenedScenePath;
};

