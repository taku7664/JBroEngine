#include "pch.h"
#include "EditorFileCommands.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Core/Asset/AssetPath.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "File/FileUtillities.h"

namespace
{
	File::Path MakeCommandTrashPath(const File::Path& originalPath)
	{
		std::error_code errorCode;
		File::Path trashRoot = std::filesystem::temp_directory_path(errorCode);
		if (errorCode)
		{
			trashRoot = File::Path(".EditorTrash");
		}
		trashRoot /= File::Path("JBroEngineEditorTrash");
		trashRoot /= File::Path(File::GenerateGuid());
		return trashRoot / originalPath.filename();
	}

	bool MovePath(const File::Path& from, const File::Path& to)
	{
		std::error_code errorCode;
		if (false == std::filesystem::exists(from, errorCode) || std::filesystem::exists(to, errorCode))
		{
			return false;
		}

		std::filesystem::create_directories(to.parent_path(), errorCode);
		if (errorCode)
		{
			return false;
		}

		std::filesystem::rename(from, to, errorCode);
		return false == static_cast<bool>(errorCode);
	}
}

CCreateFolderCommand::CCreateFolderCommand(const File::Path& requestedPath)
	: m_requestedPath(requestedPath)
{
}

CCreateFolderCommand::~CCreateFolderCommand()
{
	if (false == m_trashPath.empty())
	{
		std::error_code errorCode;
		std::filesystem::remove_all(m_trashPath, errorCode);
	}
}

const char* CCreateFolderCommand::GetName() const
{
	return "Create Folder";
}

bool CCreateFolderCommand::Execute()
{
	m_createdPath = File::GenerateUniquePath(m_requestedPath);
	m_isCreated = File::CreateFolder(m_createdPath);
	if (false == m_isCreated)
	{
		CSystemLog::Warning("Failed to create folder.");
	}
	return m_isCreated;
}

void CCreateFolderCommand::Undo()
{
	if (m_isCreated)
	{
		m_trashPath = MakeCommandTrashPath(m_createdPath);
		m_isCreated = false == MovePath(m_createdPath, m_trashPath);
	}
}

void CCreateFolderCommand::Redo()
{
	if (false == m_trashPath.empty() && std::filesystem::exists(m_trashPath))
	{
		m_isCreated = MovePath(m_trashPath, m_createdPath);
		if (m_isCreated)
		{
			m_trashPath = File::NULL_PATH;
		}
	}
	else if (false == m_createdPath.empty())
	{
		m_isCreated = File::CreateFolder(m_createdPath);
	}
}

CRenamePathCommand::CRenamePathCommand(const File::Path& sourcePath, const File::Path& targetPath, const File::Path& assetRootPath)
	: m_sourcePath(sourcePath)
	, m_targetPath(targetPath)
	, m_assetRootPath(assetRootPath)
{
}

const char* CRenamePathCommand::GetName() const
{
	return "Rename Path";
}

bool CRenamePathCommand::Execute()
{
	m_isRenamed = Rename(m_sourcePath, m_targetPath);
	if (false == m_isRenamed)
	{
		CSystemLog::Warning("Failed to rename asset browser entry.");
	}
	return m_isRenamed;
}

void CRenamePathCommand::Undo()
{
	if (m_isRenamed)
	{
		m_isRenamed = false == Rename(m_targetPath, m_sourcePath);
	}
}

void CRenamePathCommand::Redo()
{
	if (false == m_isRenamed)
	{
		m_isRenamed = Rename(m_sourcePath, m_targetPath);
	}
}

bool CRenamePathCommand::Rename(const File::Path& from, const File::Path& to)
{
	if (from == to)
	{
		return false;
	}

	if (false == MovePath(from, to))
	{
		return false;
	}

	MoveSidecarMeta(from, to);
	return true;
}

bool CRenamePathCommand::MoveSidecarMeta(const File::Path& fromAssetPath, const File::Path& toAssetPath)
{
	File::Path fromRelativePath;
	File::Path toRelativePath;
	if (false == MakeAssetRelativePath(fromAssetPath, fromRelativePath) || false == MakeAssetRelativePath(toAssetPath, toRelativePath))
	{
		return false;
	}

	const File::Path fromMetaPath = m_assetRootPath / File::Path(CAssetPath::MakeMetaPath(fromRelativePath.generic_string().c_str()));
	const File::Path toMetaPath = m_assetRootPath / File::Path(CAssetPath::MakeMetaPath(toRelativePath.generic_string().c_str()));
	return MovePath(fromMetaPath, toMetaPath);
}

bool CRenamePathCommand::MakeAssetRelativePath(const File::Path& absolutePath, File::Path& outRelativePath) const
{
	if (m_assetRootPath.empty() || absolutePath.empty())
	{
		return false;
	}

	std::error_code errorCode;
	outRelativePath = File::Path(std::filesystem::relative(absolutePath, m_assetRootPath, errorCode));
	return false == static_cast<bool>(errorCode);
}

CDeletePathCommand::CDeletePathCommand(const File::Path& targetPath, const File::Path& assetRootPath)
	: m_targetPath(targetPath)
	, m_assetRootPath(assetRootPath)
	, m_trashPath(MakeCommandTrashPath(targetPath))
{
}

CDeletePathCommand::~CDeletePathCommand()
{
	CleanupTrash();
}

const char* CDeletePathCommand::GetName() const
{
	return "Delete Path";
}

bool CDeletePathCommand::Execute()
{
	m_isDeleted = MoveToTrash();
	if (false == m_isDeleted)
	{
		CSystemLog::Warning("Failed to delete asset browser entry.");
	}
	return m_isDeleted;
}

void CDeletePathCommand::Undo()
{
	if (m_isDeleted)
	{
		m_isDeleted = false == RestoreFromTrash();
	}
}

void CDeletePathCommand::Redo()
{
	if (false == m_isDeleted)
	{
		m_isDeleted = MoveToTrash();
	}
}

bool CDeletePathCommand::MoveToTrash()
{
	if (false == MovePath(m_targetPath, m_trashPath))
	{
		return false;
	}

	MoveSidecarMetaToTrash();
	return true;
}

bool CDeletePathCommand::RestoreFromTrash()
{
	if (false == MovePath(m_trashPath, m_targetPath))
	{
		return false;
	}

	RestoreSidecarMetaFromTrash();
	return true;
}

void CDeletePathCommand::CleanupTrash()
{
	if (m_isDeleted)
	{
		std::error_code errorCode;
		std::filesystem::remove_all(m_trashPath, errorCode);
		std::filesystem::remove_all(m_metaTrashPath, errorCode);
	}
}

bool CDeletePathCommand::MoveSidecarMetaToTrash()
{
	File::Path relativePath;
	if (false == MakeAssetRelativePath(m_targetPath, relativePath))
	{
		return false;
	}

	m_metaPath = m_assetRootPath / File::Path(CAssetPath::MakeMetaPath(relativePath.generic_string().c_str()));
	if (false == std::filesystem::exists(m_metaPath))
	{
		return false;
	}

	m_metaTrashPath = m_trashPath;
	m_metaTrashPath += File::Path(".Jmeta");
	return MovePath(m_metaPath, m_metaTrashPath);
}

bool CDeletePathCommand::RestoreSidecarMetaFromTrash()
{
	if (m_metaPath.empty() || m_metaTrashPath.empty())
	{
		return false;
	}

	return MovePath(m_metaTrashPath, m_metaPath);
}

bool CDeletePathCommand::MakeAssetRelativePath(const File::Path& absolutePath, File::Path& outRelativePath) const
{
	if (m_assetRootPath.empty() || absolutePath.empty())
	{
		return false;
	}

	std::error_code errorCode;
	outRelativePath = File::Path(std::filesystem::relative(absolutePath, m_assetRootPath, errorCode));
	return false == static_cast<bool>(errorCode);
}

#endif
