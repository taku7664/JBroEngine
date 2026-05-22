#pragma once

#include "File/FilePath.h"

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
};

