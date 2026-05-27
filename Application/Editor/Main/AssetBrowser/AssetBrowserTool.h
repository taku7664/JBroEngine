#pragma once

#include "Engine/Core/Asset/AssetTypes.h"
#include "File/FilePath.h"
#include "Utillity/SafePtr.h"

#include <ctime>

struct AssetBrowserEntry
{
	File::Path AbsolutePath;
	File::Path RelativePath;
	File::Guid Guid = File::NULL_GUID;
	EAssetType Type = EAssetType::Unknown;
	std::string DisplayNameUtf8;
	std::string ExtensionUtf8;
	std::time_t LastWriteTime = {};
	bool IsDirectory = false;
	bool HasMeta = false;
	bool IsImportable = false;

	// ── 렌더 핫패스에서 사용되는 캐시 필드 ────────────────────────────────────
	// RefreshCurrentFolderEntries 가 entry 생성 시 한 번만 채운다.
	// 매 프레임 ImGui::PushID / TextUnformatted 등에서 그대로 사용 → 매 프레임 변환 비용 제거.
	std::string AbsolutePathUtf8;       // PushID 키 (안정적 unique id)
	std::string DisplayNameLowerUtf8;   // 검색 필터(toLower) 비교용
	std::string ModifiedTimeText;       // "%F %T" 포맷된 시각
};

class CAssetBrowserTool : public CImWindow
{
public:
	using CImWindow::CImWindow;
	virtual ~CAssetBrowserTool() = default;

public:
	void SetFocusFolderPath(const File::Path& path, bool pushHistory = true);

	class IAssetOpenHandler
	{
	public:
		virtual ~IAssetOpenHandler() = default;
		virtual bool CanOpen(const AssetBrowserEntry& entry) const = 0;
		virtual void Open(CAssetBrowserTool& browser, const AssetBrowserEntry& entry) = 0;
	};

	class CAssetOpenDispatcher
	{
	public:
		void RegisterHandler(OwnerPtr<IAssetOpenHandler> handler);
		void Open(CAssetBrowserTool& browser, const AssetBrowserEntry& entry);

	private:
		std::vector<OwnerPtr<IAssetOpenHandler>> m_handlers;
	};

private:
	void OnCreate() override;
	void OnDestroy() override;
	void OnUpdate() override;
	void OnRenderStay() override;

private:
	enum class EViewMode
	{
		List,
		Icon
	};

	enum class ESortMode
	{
		Name,
		Type,
		Modified
	};

	enum class EPendingOperationType
	{
		CreateFolder,
		Rename,
		Delete,
		CopyPath,
		Open,
		OpenInExplorer
	};

	struct PendingOperation
	{
		EPendingOperationType Type = EPendingOperationType::Open;
		File::Path Path;
		File::Path TargetPath;
	};

private:
	void ResetProjectState();
	void SyncProjectState();
	void RefreshCurrentFolderEntries();
	void RebuildFilteredEntries();
	void ProcessPendingOperations();

	void DrawNoProjectLoaded();
	void DrawToolbar();
	void DrawBrowserColumns();
	void DrawFolderTree();
	void DrawFolderTreeNode(const File::Path& folderPath);
	void DrawEntries();
	void DrawListEntries();
	void DrawIconEntries();
	void DrawEntryContextMenu(const AssetBrowserEntry& entry);
	void DrawBackgroundContextMenu();
	void DrawDeleteConfirmPopup();

	void SelectEntry(const AssetBrowserEntry& entry);
	void OpenEntry(const AssetBrowserEntry& entry);
	void StartRename(const AssetBrowserEntry& entry);
	void CancelRename();
	void CommitRename(const File::Path& sourcePath);
	void QueueOperation(const PendingOperation& operation);

	bool IsProjectLoaded() const;
	bool IsInsideAssetRoot(const File::Path& path) const;
	bool IsInsideScriptRoot(const File::Path& path) const;
	// Assets 또는 Scripts 루트 중 하나라도 포함하면 true.
	// 파일 작업 권한 검사용 — 두 루트를 모두 허용해야 하는 곳에서 사용.
	bool IsInsideAnyRoot(const File::Path& path) const;
	// path 가 속한 루트를 outRoot 에 채우고 그 루트 기준 상대경로를 outRelative 에 채운다.
	// 어디에도 속하지 않으면 false.
	bool ResolveRoot(const File::Path& path, File::Path& outRoot, File::Path& outRelative) const;
	bool MakeAssetRelativePath(const File::Path& absolutePath, File::Path& outRelativePath) const;
	bool ShouldShowPath(const File::Path& absolutePath, bool isDirectory) const;
	const AssetBrowserEntry* FindSelectedEntry() const;

private:
	CAssetOpenDispatcher m_openDispatcher;

	File::Path m_assetRootPath;
	File::Path m_scriptRootPath;
	File::Path m_focusFolderPath;
	File::Path m_selectedEntryPath;

	std::vector<AssetBrowserEntry> m_entries;
	std::vector<std::size_t> m_filteredEntryIndices;
	std::unordered_map<File::Path, std::vector<File::Path>> m_folderChildrenCache;
	std::deque<File::Path> m_backStack;
	std::deque<File::Path> m_forwardStack;
	std::vector<PendingOperation> m_pendingOperations;

	std::string m_searchText;
	std::string m_lastSearchText;
	std::string m_renameBuffer;
	File::Path m_renamingPath;
	File::Path m_deleteTargetPath;

	EViewMode m_viewMode = EViewMode::Icon;
	ESortMode m_sortMode = ESortMode::Name;
	std::uint64_t m_seenAssetDatabaseRevision = 0;
	bool m_showMetaFiles = false;
	bool m_entriesDirty = true;
	bool m_filterDirty = true;
	bool m_isRenaming = false;
	bool m_requestDeletePopup = false;
};

