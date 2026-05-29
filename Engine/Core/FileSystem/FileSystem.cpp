#include "pch.h"
#include "FileSystem.h"

#include "Core/Asset/AssetPath.h"

bool CFileSystem::Initialize(const File::Path& rootPath, EFileSystemAccess access)
{
	m_access = access;
	m_originPath = File::Path(std::filesystem::current_path().generic_string());
	if (false == rootPath.empty())
	{
		m_rootPath = File::Path(rootPath.generic_string());
	}

	m_isInitialized = true;
	return true;
}

void CFileSystem::Finalize()
{
	m_isInitialized = false;
}

bool CFileSystem::ResolvePath(const File::Path& relativePath, File::Path& outResolvedPath) const
{
	std::string normalizedPath;
	if (false == CAssetPath::NormalizeRelativePath(relativePath.generic_string().c_str(), normalizedPath))
	{
		return false;
	}

	std::filesystem::path resolvedPath(m_rootPath);
	resolvedPath /= std::filesystem::path(normalizedPath);
	outResolvedPath = File::Path(resolvedPath.generic_string());
	return true;
}

bool CFileSystem::Exists(const File::Path& relativePath) const
{
	File::Path resolvedPath;
	if (false == ResolvePath(relativePath, resolvedPath))
	{
		return false;
	}

	std::error_code errorCode;
	return std::filesystem::exists(resolvedPath, errorCode);
}

bool CFileSystem::ReadAllBytes(const File::Path& relativePath, FileReadResult& outResult) const
{
	outResult = {};
	File::Path resolvedPath;
	if (false == ResolvePath(relativePath, resolvedPath))
	{
		return false;
	}

	std::ifstream file(resolvedPath, std::ios::binary | std::ios::ate);
	if (false == file.is_open())
	{
		return false;
	}

	const std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);
	if (size > 0)
	{
		outResult.Bytes.resize(static_cast<std::size_t>(size));
		file.read(reinterpret_cast<char*>(outResult.Bytes.data()), size);
	}

	outResult.Success = true;
	return true;
}

bool CFileSystem::ReadAllText(const File::Path& relativePath, std::string& outText) const
{
	FileReadResult result;
	if (false == ReadAllBytes(relativePath, result))
	{
		return false;
	}

	if (result.Bytes.empty())
	{
		outText.clear();
		return true;
	}

	outText.assign(reinterpret_cast<const char*>(result.Bytes.data()), result.Bytes.size());
	return true;
}

const File::Path& CFileSystem::GetOriginPath() const
{
	return m_originPath;
}

const File::Path& CFileSystem::GetRootPath() const
{
	return m_rootPath;
}

EFileSystemAccess CFileSystem::GetAccess() const
{
	return m_access;
}
