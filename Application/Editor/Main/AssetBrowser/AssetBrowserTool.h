#pragma once

#include "Engine/Core/Asset/AssetTypes.h"
#include "Engine/Editor/ImWindow/ImWindow.h"   // CImWindow 기반 클래스
#include "ThirdParty/imgui/imgui.h"            // ImGuiSelectionBasicStorage, ImGuiID (다중 선택)
#include "Utillity/File/FilePath.h"
#include "Utillity/Pointer/SafePtr.h"

#include <ctime>

class CSpriteAsset;

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

	// 다중 선택 저장소(ImGuiSelectionBasicStorage)용 안정적 식별자.
	// RefreshCurrentFolderEntries 에서 AbsolutePathUtf8 의 해시로 1회 채운다.
	// 경로 기반이라 같은 폴더 리프레시 후에도 동일 → 선택이 보존된다.
	ImGuiID SelectionId = 0;

	// ── 렌더 핫패스에서 사용되는 캐시 필드 ────────────────────────────────────
	// RefreshCurrentFolderEntries 가 entry 생성 시 한 번만 채운다.
	// 매 프레임 ImGui::PushID / TextUnformatted 등에서 그대로 사용 → 매 프레임 변환 비용 제거.
	std::string AbsolutePathUtf8;       // PushID 키 (안정적 unique id)
	std::string DisplayNameLowerUtf8;   // 검색 필터(toLower) 비교용
	std::string ModifiedTimeText;       // "%F %T" 포맷된 시각

	// 썸네일 / 아이콘.
	// - 이미지 파일(.png/.jpg/...): 해당 SpriteAsset 을 직접 로드해 프리뷰로 사용.
	// - 그 외(폴더/스크립트/씬/...): Core::ResourceRegistry 의 영구 아이콘을 참조.
	// 두 경우 모두 SafePtr 만 보관하므로 entry 사본은 가볍다.
	SafePtr<CSpriteAsset> Thumbnail;
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
		OpenInExplorer,
		Duplicate,
		// 루트가 Scripts 일 때 컨텍스트 메뉴에서 호출.
		// TargetPath 는 사용하지 않고 Path 는 부모 폴더 절대경로.
		CreateScript,
		// 루트가 Assets 일 때.  Path 는 부모 폴더, TargetPath 는 새 파일명(상대).
		CreateScene,
		CreateMaterial,
		CreatePrefab,
		// 드래그&드롭 이동 / 잘라내기-붙여넣기.  Path = 원본, TargetPath = 대상 폴더.
		MoveInto,
		// 복사-붙여넣기.  Path = 원본, TargetPath = 대상 폴더.
		CopyInto
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
	// SceneView 의 ##SVCtxMenu 와 동일한 패턴 — 우클릭 시점에 컨텍스트 상태만
	// 세팅하고 단일 OpenPopup 호출. BeginPopup 내부에서 상태별 섹션 분기.
	//
	// 폴더 트리(좌측 패널)와 브라우저 바디(우측 entry 영역)는 의미가 다르므로
	// 팝업 ID 를 분리한다 — 한쪽 팝업이 열린 상태에서 다른 패널 우클릭 시
	// 두 팝업이 동시에 살아있다가 깔끔히 교체되도록.
	void DrawBrowserBodyContextMenu();
	void DrawFolderTreeContextMenu();

	void OpenBodyContextMenuForEntry(const AssetBrowserEntry& entry);
	void OpenBodyContextMenuForBackground();
	void OpenFolderTreeContextMenu(const File::Path& folderPath);
	void DrawDeleteConfirmPopup();

	// 단일 선택으로 교체(우클릭/프로그램적 선택). 기존 다중 선택을 모두 지운다.
	void SelectEntry(const AssetBrowserEntry& entry);
	// BeginMultiSelect/EndMultiSelect 가 돌려준 요청을 m_selection 에 반영하고,
	// 사용자 상호작용으로 선택이 바뀐 프레임이면 Editor 선택 동기화를 예약한다.
	void ApplyMultiSelectRequests(ImGuiMultiSelectIO* io);
	// m_selection → Editor 선택 반영. 단일=SelectAsset, 다중/0=에셋 선택 해제.
	void SyncEditorSelection();
	// m_selection 에 포함된 엔트리들의 절대경로 목록(삭제 등 다중 처리용).
	std::vector<File::Path> CollectSelectedPaths() const;

	// ── 이동 / 잘라내기 / 복사 / 붙여넣기 ──────────────────────────────────────
	// 우클릭 대상이 다중 선택에 포함돼 있으면 선택 전체를, 아니면 그 항목만 반환.
	std::vector<File::Path> CollectOperationTargets(const File::Path& contextEntryPath) const;
	// 드래그된 항목(m_dragPrimaryPath)이 선택에 포함되면 선택 전체를, 아니면 그 항목만
	// targetFolder 로 이동(MoveInto) 큐잉.
	void DropAssetsIntoFolder(const File::Path& targetFolder);
	// paths 를 잘라내기/복사 클립보드에 적재.
	void CutToClipboard(std::vector<File::Path> paths);
	void CopyToClipboard(std::vector<File::Path> paths);
	// 클립보드 항목을 targetFolder 로 이동(cut)/복사(copy).  cut 은 적용 후 비운다.
	void PasteIntoFolder(const File::Path& targetFolder);
	// source 를 targetFolder 에 배치 가능한지. 공통: 같은 루트 + 폴더 자신/하위 금지.
	// isMove==true 면 "이미 그 폴더에 있음"도 금지(이동). 복사는 같은 폴더 사본을 허용.
	bool CanPlaceInto(const File::Path& source, const File::Path& targetFolder, bool isMove) const;

	void OpenEntry(const AssetBrowserEntry& entry);
	void StartRename(const AssetBrowserEntry& entry);
	// 갓 만든 파일에 대해 entry 가 등장하기 전이라도 rename 모드를 예약한다.
	// 다음 RefreshCurrentFolderEntries 후 InputText 가 자동으로 활성화된다.
	void StartRenameForNewPath(const File::Path& path);
	void CancelRename();
	// CreateScript 우클릭 동작 — ImEditor::OpenPopup 으로 신규 스크립트 입력 모달.
	void ShowNewScriptPopup(const File::Path& parentFolder);
	// LiveCompile 실패 메시지를 컴파일러 출력 + 복사/확인 버튼과 함께 모달로 표시.
	void ShowScriptCompileFailurePopup(std::string message);
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

private:
	CAssetOpenDispatcher m_openDispatcher;

	File::Path m_assetRootPath;
	File::Path m_scriptRootPath;
	File::Path m_focusFolderPath;
	// primary(활성) 선택 — Inspector 표시/이름변경 대상. 다중 선택의 대표 1개.
	File::Path m_selectedEntryPath;

	// 다중 선택 저장소. id = AssetBrowserEntry::SelectionId.
	// ImGui Multi-Select(Box/Ctrl/Shift) 결과가 여기에 누적된다.
	ImGuiSelectionBasicStorage m_selection;
	// 이번 프레임 선택이 사용자 입력으로 바뀌었는지 — EndMultiSelect 후 Editor 동기화 트리거.
	bool m_selectionChangedThisFrame = false;

	// 드래그 시작된 항목의 절대경로 — 폴더 드롭 시 이동 소스 판정에 사용.
	File::Path m_dragPrimaryPath;

	// 박스 선택(고무줄 다중 선택)은 "빈 공간"에서 드래그를 시작할 때만 켠다.
	// 아이템 셀 위에서 누르면 단일 에셋 드래그-드랍(Ref 드롭 타깃용)이 동작해야 하므로
	// 그 드래그 동안에는 BoxSelect 플래그를 빼야 ImGui 가 박스 선택으로 드래그를 가로채지 않는다.
	// press 가 아이템 위였는지는 "직전 프레임에 항목이 호버되어 있었는지"로 판정한다(공개 API).
	bool m_entryHoveredThisFrame = false;
	bool m_entryHoveredPrevFrame = false;
	bool m_boxSelectFromVoid     = true;
	// 잘라내기/복사 클립보드. m_clipboardIsCut == true 면 이동(붙여넣기 후 비움), false 면 복사.
	std::vector<File::Path> m_clipboardPaths;
	bool m_clipboardIsCut = false;

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
	// 삭제 확인 팝업 대상 — 다중 선택 삭제를 위해 목록으로 보관.
	std::vector<File::Path> m_deleteTargets;

	// ── 컨텍스트 메뉴 상태 ────────────────────────────────────────────────────
	// Body 팝업: entry 우클릭 시 m_bodyCtxEntryPath = entry.AbsolutePath,
	//            빈공간 우클릭 시 m_bodyCtxEntryPath = NULL_PATH.
	// Tree 팝업: 폴더 우클릭 시 m_treeCtxFolderPath = 해당 폴더.
	// OpenPopup 은 다음 프레임에 적용되므로, 직접 호출 대신 *Requested 플래그를
	// 세팅하고 DrawXxxContextMenu 첫 부분에서 OpenPopup 을 일괄 실행한다.
	File::Path m_bodyCtxEntryPath;
	File::Path m_treeCtxFolderPath;
	bool       m_bodyCtxOpenRequested = false;
	bool       m_treeCtxOpenRequested = false;

	EViewMode m_viewMode = EViewMode::Icon;
	ESortMode m_sortMode = ESortMode::Name;
	std::uint64_t m_seenAssetDatabaseRevision = 0;
	bool m_showMetaFiles = false;
	bool m_entriesDirty = true;
	bool m_filterDirty = true;
	bool m_isRenaming = false;
	bool m_requestDeletePopup = false;
};

