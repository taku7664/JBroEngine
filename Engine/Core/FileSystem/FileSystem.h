#pragma once

#include "Core/FileSystem/FileSystemTypes.h"
#include "Utillity/SafePtr.h"

class CFileSystem final : public EnableSafeFromThis<CFileSystem>
{
public:
	bool Initialize(const char* rootPath, EFileSystemAccess access);
	void Finalize();

	bool ResolvePath(const char* relativePath, std::string& outResolvedPath) const;
	bool Exists(const char* relativePath) const;
	bool ReadAllBytes(const char* relativePath, FileReadResult& outResult) const;
	bool ReadAllText(const char* relativePath, std::string& outText) const;
	const std::string& GetOriginPath() const;
	const std::string& GetRootPath() const;
	EFileSystemAccess GetAccess() const;

private:
	std::string m_originPath = ".";
	std::string m_rootPath = ".";
	EFileSystemAccess m_access = EFileSystemAccess::ReadOnly;
	bool m_isInitialized = false;
};
