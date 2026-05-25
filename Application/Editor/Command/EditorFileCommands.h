#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Command/EditorCommandManager.h"
#include "File/FilePath.h"

class CCreateFolderCommand final : public IEditorCommand
{
public:
	explicit CCreateFolderCommand(const File::Path& requestedPath);
	~CCreateFolderCommand() override;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	File::Path m_requestedPath;
	File::Path m_createdPath;
	File::Path m_trashPath;
	bool m_isCreated = false;
};

class CRenamePathCommand final : public IEditorCommand
{
public:
	CRenamePathCommand(const File::Path& sourcePath, const File::Path& targetPath, const File::Path& assetRootPath);
	~CRenamePathCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	bool Rename(const File::Path& from, const File::Path& to);
	bool MoveSidecarMeta(const File::Path& fromAssetPath, const File::Path& toAssetPath);
	bool MakeAssetRelativePath(const File::Path& absolutePath, File::Path& outRelativePath) const;

private:
	File::Path m_sourcePath;
	File::Path m_targetPath;
	File::Path m_assetRootPath;
	bool m_isRenamed = false;
};

class CDeletePathCommand final : public IEditorCommand
{
public:
	CDeletePathCommand(const File::Path& targetPath, const File::Path& assetRootPath);
	~CDeletePathCommand() override;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	bool MoveToTrash();
	bool RestoreFromTrash();
	void CleanupTrash();
	bool MoveSidecarMetaToTrash();
	bool RestoreSidecarMetaFromTrash();
	bool MakeAssetRelativePath(const File::Path& absolutePath, File::Path& outRelativePath) const;

private:
	File::Path m_targetPath;
	File::Path m_assetRootPath;
	File::Path m_trashPath;
	File::Path m_metaPath;
	File::Path m_metaTrashPath;
	bool m_isDeleted = false;
};

#endif
