#include "pch.h"
#include "EditorPathUtils.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

std::string EditorPathUtils::ToUtf8(const std::filesystem::path& path)
{
	const auto text = path.generic_u8string();
	return std::string(reinterpret_cast<const char*>(text.c_str()), text.size());
}

bool EditorPathUtils::IsRelativeSubPath(const std::filesystem::path& relativePath)
{
	if (relativePath.empty() || relativePath.is_absolute())
	{
		return false;
	}

	return std::none_of(relativePath.begin(), relativePath.end(), [](const std::filesystem::path& part) {
		return part == L"..";
	});
}

bool EditorPathUtils::TryMakeRelativeSubPath(
	const std::filesystem::path& path,
	const std::filesystem::path& root,
	File::Path& outRelativePath)
{
	outRelativePath = File::NULL_PATH;
	if (path.empty() || root.empty())
	{
		return false;
	}

	std::error_code error;
	const File::Path absolutePath = std::filesystem::weakly_canonical(path, error);
	if (error)
	{
		return false;
	}
	error.clear();
	const File::Path absoluteRoot = std::filesystem::weakly_canonical(root, error);
	if (error)
	{
		return false;
	}

	const File::Path relativePath = std::filesystem::relative(absolutePath, absoluteRoot, error);
	if (error || false == IsRelativeSubPath(relativePath))
	{
		return false;
	}

	outRelativePath = relativePath;
	return true;
}

bool EditorPathUtils::IsPathInside(
	const std::filesystem::path& root,
	const std::filesystem::path& path)
{
	File::Path relativePath;
	return TryMakeRelativeSubPath(path, root, relativePath);
}

#endif
