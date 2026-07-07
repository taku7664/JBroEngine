#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Utillity/File/FilePath.h"

#include <string>

namespace EditorPathUtils
{
	std::string ToUtf8(const std::filesystem::path& path);
	bool IsRelativeSubPath(const std::filesystem::path& relativePath);
	bool TryMakeRelativeSubPath(
		const std::filesystem::path& path,
		const std::filesystem::path& root,
		File::Path& outRelativePath);
	bool IsPathInside(
		const std::filesystem::path& root,
		const std::filesystem::path& path);
}

#endif
