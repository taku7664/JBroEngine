#include "pch.h"
#include "FileSystem.h"

#include "Core/Asset/AssetPath.h"

bool CFileSystem::Initialize(const char* rootPath, EFileSystemAccess access)
{
	m_access = access;
	if (nullptr != rootPath && '\0' != rootPath[0])
	{
		m_rootPath = std::filesystem::path(rootPath).generic_string();
	}

	m_isInitialized = true;
	return true;
}

void CFileSystem::Finalize()
{
	m_isInitialized = false;
}

bool CFileSystem::ResolvePath(const char* relativePath, std::string& outResolvedPath) const
{
	std::string normalizedPath;
	if (false == CAssetPath::NormalizeRelativePath(relativePath, normalizedPath))
	{
		return false;
	}

	std::filesystem::path resolvedPath(m_rootPath);
	resolvedPath /= std::filesystem::path(normalizedPath);
	outResolvedPath = resolvedPath.generic_string();
	return true;
}

bool CFileSystem::Exists(const char* relativePath) const
{
	std::string resolvedPath;
	if (false == ResolvePath(relativePath, resolvedPath))
	{
		return false;
	}

	std::error_code errorCode;
	return std::filesystem::exists(resolvedPath, errorCode);
}

bool CFileSystem::ReadAllBytes(const char* relativePath, FileReadResult& outResult) const
{
	outResult = {};
	std::string resolvedPath;
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

bool CFileSystem::ReadAllText(const char* relativePath, std::string& outText) const
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

const std::string& CFileSystem::GetRootPath() const
{
	return m_rootPath;
}

EFileSystemAccess CFileSystem::GetAccess() const
{
	return m_access;
}
