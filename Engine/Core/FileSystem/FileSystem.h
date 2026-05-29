#pragma once

#include "Core/FileSystem/FileSystemTypes.h"
#include "File/FilePath.h"
#include "Utillity/SafePtr.h"

class CFileSystem final : public EnableSafeFromThis<CFileSystem>
{
public:
	bool Initialize(const File::Path& rootPath, EFileSystemAccess access);
	void Finalize();

	bool ResolvePath(const File::Path& relativePath, File::Path& outResolvedPath) const;
	bool Exists(const File::Path& relativePath) const;
	bool ReadAllBytes(const File::Path& relativePath, FileReadResult& outResult) const;
	bool ReadAllText(const File::Path& relativePath, std::string& outText) const;
	const File::Path& GetOriginPath() const;
	const File::Path& GetRootPath() const;
	EFileSystemAccess GetAccess() const;

private:
	File::Path m_originPath = ".";
	File::Path m_rootPath = ".";
	EFileSystemAccess m_access = EFileSystemAccess::ReadOnly;
	bool m_isInitialized = false;
};
