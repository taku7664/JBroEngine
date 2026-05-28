#include "pch.h"
#include "AssetBrowserTool.h"
#include "AssetHandler.h"

#include "Editor/Editor.h"
#include "Editor/Command/EditorFileCommands.h"
#include "Editor/EditorDragDrop.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/Core/Core.h"
#include "Engine/Core/EngineContext.h"
#include "Engine/Core/Asset/AssetPath.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/IAssetRegistry.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "Engine/GameFramework/Rendering/SpriteRenderSystem.h"
#include "File/FileUtillities.h"
#include "StringUtillity.h"

#include <chrono>

namespace
{
	constexpr float TOOLBAR_HEIGHT = 32.0f;
	constexpr float FOLDER_PANEL_WIDTH = 260.0f;
	constexpr float ICON_CELL_WIDTH = 112.0f;
	constexpr float ICON_CELL_HEIGHT = 92.0f;
	constexpr const char* ENTRY_CONTEXT_POPUP_ID = "AssetBrowserEntryContext";
	constexpr const char* BACKGROUND_CONTEXT_POPUP_ID = "AssetBrowserBackgroundContext";

	const char* GetEntryIcon(const AssetBrowserEntry& entry)
	{
		if (entry.IsDirectory)
		{
			return "[DIR]";
		}

		switch (entry.Type)
		{
		case EAssetType::Texture: return "[TEX]";
		case EAssetType::Sprite: return "[SPR]";
		case EAssetType::Material: return "[MAT]";
		case EAssetType::Scene: return "[SCN]";
		case EAssetType::Prefab: return "[PFB]";
		case EAssetType::Script: return "[SCR]";
		default: return "[FILE]";
		}
	}

	void BeginAssetDragDropSource(const AssetBrowserEntry& entry)
	{
		EditorDragDrop::AssetPayloadDesc desc;
		desc.Guid = entry.Guid;
		desc.RelativePath = entry.RelativePath;
		desc.Type = entry.Type;
		desc.IsDirectory = entry.IsDirectory;
		desc.PreviewLabel = entry.DisplayNameUtf8.c_str();
		EditorDragDrop::BeginAssetDragDropSource(desc);
	}
}

void CAssetBrowserTool::CAssetOpenDispatcher::RegisterHandler(OwnerPtr<IAssetOpenHandler> handler)
{
	if (handler)
	{
		m_handlers.push_back(std::move(handler));
	}
}

void CAssetBrowserTool::CAssetOpenDispatcher::Open(CAssetBrowserTool& browser, const AssetBrowserEntry& entry)
{
	for (const OwnerPtr<IAssetOpenHandler>& handler : m_handlers)
	{
		if (handler && handler->CanOpen(entry))
		{
			handler->Open(browser, entry);
			return;
		}
	}
}

void CAssetBrowserTool::OnCreate()
{
	SetLocalizedTitleKey("window.asset_browser");
	// 등록 순서 = 우선순위 (먼저 CanOpen=true 인 핸들러가 잡음).
	// Default 는 catch-all 이므로 항상 마지막.
	m_openDispatcher.RegisterHandler(MakeOwnerPtr<CSceneAssetOpenHandler>());
	m_openDispatcher.RegisterHandler(MakeOwnerPtr<CScriptAssetOpenHandler>());
	m_openDispatcher.RegisterHandler(MakeOwnerPtr<CDefaultAssetOpenHandler>());
}

void CAssetBrowserTool::OnDestroy()
{
	m_openDispatcher = CAssetOpenDispatcher();
	ResetProjectState();
}

void CAssetBrowserTool::OnUpdate()
{
	SyncProjectState();
	ProcessPendingOperations();
}

void CAssetBrowserTool::OnRenderStay()
{
	if (false == IsProjectLoaded())
	{
		DrawNoProjectLoaded();
		return;
	}

	if (m_entriesDirty)
	{
		RefreshCurrentFolderEntries();
	}
	if (m_filterDirty || m_lastSearchText != m_searchText)
	{
		RebuildFilteredEntries();
	}

	DrawToolbar();
	DrawBrowserColumns();
	DrawDeleteConfirmPopup();
}

void CAssetBrowserTool::SetFocusFolderPath(const File::Path& path, bool pushHistory)
{
	// Assets / Scripts 둘 다 포커스 가능
	if (false == IsInsideAnyRoot(path))
	{
		return;
	}

	std::error_code errorCode;
	if (false == std::filesystem::exists(path, errorCode) || false == std::filesystem::is_directory(path, errorCode))
	{
		return;
	}

	if (path == m_focusFolderPath)
	{
		return;
	}

	if (pushHistory && false == m_focusFolderPath.empty())
	{
		m_backStack.push_back(m_focusFolderPath);
		m_forwardStack.clear();
	}

	m_focusFolderPath = path;
	m_selectedEntryPath = File::NULL_PATH;
	CancelRename();
	m_entriesDirty = true;
}

void CAssetBrowserTool::ResetProjectState()
{
	m_assetRootPath = File::NULL_PATH;
	m_scriptRootPath = File::NULL_PATH;
	m_focusFolderPath = File::NULL_PATH;
	m_selectedEntryPath = File::NULL_PATH;
	m_entries.clear();
	m_filteredEntryIndices.clear();
	m_folderChildrenCache.clear();
	m_backStack.clear();
	m_forwardStack.clear();
	m_pendingOperations.clear();
	m_searchText.clear();
	m_lastSearchText.clear();
	m_deleteTargetPath = File::NULL_PATH;
	m_requestDeletePopup = false;
	CancelRename();
	m_seenAssetDatabaseRevision = 0;
	m_entriesDirty = true;
	m_filterDirty = true;
}

void CAssetBrowserTool::SyncProjectState()
{
	SafePtr<CProjectManager> projectManager = GetProjectManager();
	if (false == projectManager.IsValid() || false == projectManager->IsProjectLoaded())
	{
		if (false == m_assetRootPath.empty() || false == m_scriptRootPath.empty())
		{
			ResetProjectState();
		}
		return;
	}

	const File::Path& assetPath = projectManager->GetAssetPath();
	const File::Path& scriptPath = projectManager->GetScriptPath();
	if (m_assetRootPath != assetPath || m_scriptRootPath != scriptPath)
	{
		ResetProjectState();
		m_assetRootPath = assetPath;
		m_scriptRootPath = scriptPath;
		m_focusFolderPath = assetPath;
	}

	const std::uint64_t revision = projectManager->GetAssetDatabaseRevision();
	if (m_seenAssetDatabaseRevision != revision)
	{
		m_seenAssetDatabaseRevision = revision;
		m_folderChildrenCache.clear();
		m_entriesDirty = true;
	}
}

void CAssetBrowserTool::RefreshCurrentFolderEntries()
{
	m_entries.clear();
	m_filteredEntryIndices.clear();

	if (m_focusFolderPath.empty())
	{
		m_entriesDirty = false;
		m_filterDirty = true;
		return;
	}

	SafePtr<CProjectManager> projectManager = GetProjectManager();
	SafePtr<IAssetManager> assetManager = projectManager ? projectManager->GetAssetManager() : nullptr;
	const IAssetRegistry* registry = assetManager ? &assetManager->GetRegistry() : nullptr;

	std::error_code errorCode;
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(m_focusFolderPath, errorCode))
	{
		// iterator step 자체가 실패한 경우만 continue.
		// 이전엔 break 였지만, 권한 거부된 한 항목 때문에 폴더 전체 read 가 끊기는 문제가 있었음.
		if (errorCode)
		{
			errorCode.clear();
			continue;
		}

		const File::Path absolutePath(entry.path());
		const bool isDirectory = entry.is_directory(errorCode);
		if (errorCode)
		{
			errorCode.clear();
			continue;
		}

		if (false == ShouldShowPath(absolutePath, isDirectory))
		{
			continue;
		}

		// 현재 폴더가 Asset root 안인지 Script root 안인지 한 번만 판정 (루프 밖에서 캐싱 가능)
		const bool insideAssetRoot = IsInsideAssetRoot(absolutePath);

		AssetBrowserEntry browserEntry;
		browserEntry.AbsolutePath = absolutePath;
		browserEntry.IsDirectory = isDirectory;
		browserEntry.IsImportable = insideAssetRoot && false == isDirectory && false == CAssetPath::IsMetaPath(absolutePath.generic_string().c_str());
		MakeAssetRelativePath(absolutePath, browserEntry.RelativePath);
		browserEntry.DisplayNameUtf8 = ToUtf8(absolutePath.filename());
		browserEntry.ExtensionUtf8 = ToLower(ToUtf8(absolutePath.extension()));

		// 렌더 핫패스용 캐시 (PushID 키, 검색 비교용)
		browserEntry.AbsolutePathUtf8     = ToUtf8(absolutePath);
		browserEntry.DisplayNameLowerUtf8 = ToLower(browserEntry.DisplayNameUtf8);

		if (false == isDirectory)
		{
			const std::filesystem::file_time_type lastWriteTime = entry.last_write_time(errorCode);
			if (false == static_cast<bool>(errorCode))
			{
				browserEntry.LastWriteTime = ToTimeT(lastWriteTime);
				// "%F %T" 포맷을 미리 캐시 — DrawListEntries 의 매 프레임 strftime 비용 제거
				std::tm timeInfo = {};
				localtime_s(&timeInfo, &browserEntry.LastWriteTime);
				char buffer[64] = {};
				std::strftime(buffer, sizeof(buffer), "%F %T", &timeInfo);
				browserEntry.ModifiedTimeText = buffer;
			}
			errorCode.clear();

			// AssetMetaData/HasMeta 는 Asset root 안의 파일에만 의미가 있다.
			// Script root 안의 .cpp/.h 등은 .Jmeta 가 없으므로 skip.
			if (insideAssetRoot && registry)
			{
				const std::string relativePath = browserEntry.RelativePath.generic_string();
				if (const AssetMetaData* metaData = registry->FindAssetByPath(relativePath.c_str()))
				{
					browserEntry.Guid = metaData->Guid;
					browserEntry.Type = metaData->Type;
				}
			}

			if (insideAssetRoot)
			{
				const std::string metaRelativePath = CAssetPath::MakeMetaPath(browserEntry.RelativePath.generic_string().c_str());
				browserEntry.HasMeta = std::filesystem::exists(m_assetRootPath / File::Path(metaRelativePath), errorCode);
				errorCode.clear();
			}
			else if (browserEntry.ExtensionUtf8 == ".cpp" || browserEntry.ExtensionUtf8 == ".h" || browserEntry.ExtensionUtf8 == ".hpp")
			{
				// Script root 내 코드 파일은 Script 자산으로 표시
				browserEntry.Type = EAssetType::Script;
			}
		}

		m_entries.push_back(std::move(browserEntry));
	}

	m_entriesDirty = false;
	m_filterDirty = true;
}

void CAssetBrowserTool::RebuildFilteredEntries()
{
	m_filteredEntryIndices.clear();
	const std::string filter = ToLower(m_searchText);
	for (std::size_t i = 0; i < m_entries.size(); ++i)
	{
		// 캐시된 DisplayNameLowerUtf8 사용 — 매번 ToLower 호출 안 함
		if (false == filter.empty() && m_entries[i].DisplayNameLowerUtf8.find(filter) == std::string::npos)
		{
			continue;
		}
		m_filteredEntryIndices.push_back(i);
	}

	std::sort(m_filteredEntryIndices.begin(), m_filteredEntryIndices.end(), [this](std::size_t lhsIndex, std::size_t rhsIndex) {
		const AssetBrowserEntry& lhs = m_entries[lhsIndex];
		const AssetBrowserEntry& rhs = m_entries[rhsIndex];
		if (lhs.IsDirectory != rhs.IsDirectory)
		{
			return lhs.IsDirectory;
		}

		switch (m_sortMode)
		{
		case ESortMode::Type:
			if (lhs.ExtensionUtf8 != rhs.ExtensionUtf8)
			{
				return lhs.ExtensionUtf8 < rhs.ExtensionUtf8;
			}
			break;
		case ESortMode::Modified:
			if (lhs.LastWriteTime != rhs.LastWriteTime)
			{
				return lhs.LastWriteTime > rhs.LastWriteTime;
			}
			break;
		default:
			break;
		}

		return lhs.DisplayNameUtf8 < rhs.DisplayNameUtf8;
	});

	m_lastSearchText = m_searchText;
	m_filterDirty = false;
}

void CAssetBrowserTool::ProcessPendingOperations()
{
	if (m_pendingOperations.empty())
	{
		return;
	}

	std::vector<PendingOperation> operations = std::move(m_pendingOperations);
	m_pendingOperations.clear();

	for (const PendingOperation& operation : operations)
	{
		// Asset / Script 두 루트 모두 허용. 단 Asset DB 명령(생성/이름변경/삭제)은
		// Asset root 내에서만 의미 있으므로 분기 처리.
		if (false == IsInsideAnyRoot(operation.Path))
		{
			continue;
		}

		const bool insideAssetRoot = IsInsideAssetRoot(operation.Path);
		std::error_code errorCode;
		switch (operation.Type)
		{
		case EPendingOperationType::CreateFolder:
			if (insideAssetRoot)
			{
				Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CCreateFolderCommand>(operation.Path), "__AssetDatabase");
			}
			break;
		case EPendingOperationType::Rename:
			if (insideAssetRoot && IsInsideAssetRoot(operation.TargetPath))
			{
				Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CRenamePathCommand>(operation.Path, operation.TargetPath, m_assetRootPath), "__AssetDatabase");
			}
			break;
		case EPendingOperationType::Delete:
			if (insideAssetRoot)
			{
				Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CDeletePathCommand>(operation.Path, m_assetRootPath), "__AssetDatabase");
			}
			break;
		case EPendingOperationType::CopyPath:
			File::CopyPathToClipBoard(operation.Path);
			break;
		case EPendingOperationType::Open:
			File::OpenFile(operation.Path);
			break;
		case EPendingOperationType::OpenInExplorer:
			File::OpenFile(std::filesystem::is_directory(operation.Path, errorCode) ? operation.Path : File::Path(operation.Path.parent_path()));
			errorCode.clear();
			break;
		default:
			break;
		}
	}

	m_entriesDirty = true;
}

void CAssetBrowserTool::DrawNoProjectLoaded()
{
	ImGui::TextDisabled(Loc::Text("asset_browser.no_project_loaded"));
	ImGui::TextDisabled(Loc::Text("asset_browser.open_project_hint"));
}

void CAssetBrowserTool::DrawToolbar()
{
	ImGui::BeginChild("AssetBrowserToolbar", ImVec2(0.0f, TOOLBAR_HEIGHT), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	const bool canBack = false == m_backStack.empty();
	const bool canForward = false == m_forwardStack.empty();
	// Up: 현재 폴더가 어느 루트(Assets/Scripts)의 루트 자체가 아닐 때만 활성화
	const bool canUp = false == m_focusFolderPath.empty()
		&& m_focusFolderPath != m_assetRootPath
		&& m_focusFolderPath != m_scriptRootPath;

	if (false == canBack) ImGui::BeginDisabled();
	if (ImGui::Button("<"))
	{
		m_forwardStack.push_back(m_focusFolderPath);
		File::Path path = m_backStack.back();
		m_backStack.pop_back();
		SetFocusFolderPath(path, false);
	}
	if (false == canBack) ImGui::EndDisabled();

	ImGui::SameLine();
	if (false == canForward) ImGui::BeginDisabled();
	if (ImGui::Button(">"))
	{
		m_backStack.push_back(m_focusFolderPath);
		File::Path path = m_forwardStack.back();
		m_forwardStack.pop_back();
		SetFocusFolderPath(path, false);
	}
	if (false == canForward) ImGui::EndDisabled();

	ImGui::SameLine();
	if (false == canUp) ImGui::BeginDisabled();
	if (ImGui::Button(Loc::Text("asset_browser.up")))
	{
		SetFocusFolderPath(File::Path(m_focusFolderPath.parent_path()));
	}
	if (false == canUp) ImGui::EndDisabled();

	ImGui::SameLine();
	if (ImGui::Button(Loc::Text("common.refresh")))
	{
		m_folderChildrenCache.clear();
		m_entriesDirty = true;
	}

	ImGui::SameLine();
	// 현재 포커스 폴더가 어느 루트(Assets/Scripts)에 속하는지에 따라 표시
	{
		File::Path rootPath;
		File::Path relativeUnderRoot;
		const char* rootLabel = "Assets";
		if (ResolveRoot(m_focusFolderPath, rootPath, relativeUnderRoot))
		{
			rootLabel = (rootPath == m_scriptRootPath) ? "Scripts" : "Assets";
		}
		const File::Path displayPath = (m_focusFolderPath == rootPath || relativeUnderRoot.empty())
			? File::Path(rootLabel)
			: File::Path(rootLabel) / relativeUnderRoot;
		ImGui::TextUnformatted(ToUtf8(displayPath).c_str());
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(180.0f);
	if (ImGui::InputTextWithHint("##AssetBrowserSearch", Loc::Text("asset_browser.search"), &m_searchText))
	{
		m_filterDirty = true;
	}

	ImGui::SameLine();
	if (ImGui::BeginCombo("##AssetBrowserViewMode", m_viewMode == EViewMode::List ? Loc::Text("asset_browser.view_list") : Loc::Text("asset_browser.view_icon")))
	{
		if (ImGui::Selectable(Loc::Text("asset_browser.view_list"), m_viewMode == EViewMode::List))
		{
			m_viewMode = EViewMode::List;
		}
		if (ImGui::Selectable(Loc::Text("asset_browser.view_icon"), m_viewMode == EViewMode::Icon))
		{
			m_viewMode = EViewMode::Icon;
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	const char* sortLabel = m_sortMode == ESortMode::Name ? Loc::Text("common.name") : (m_sortMode == ESortMode::Type ? Loc::Text("common.type") : Loc::Text("common.modified"));
	if (ImGui::BeginCombo("##AssetBrowserSortMode", sortLabel))
	{
		if (ImGui::Selectable(Loc::Text("common.name"), m_sortMode == ESortMode::Name))
		{
			m_sortMode = ESortMode::Name;
			m_filterDirty = true;
		}
		if (ImGui::Selectable(Loc::Text("common.type"), m_sortMode == ESortMode::Type))
		{
			m_sortMode = ESortMode::Type;
			m_filterDirty = true;
		}
		if (ImGui::Selectable(Loc::Text("common.modified"), m_sortMode == ESortMode::Modified))
		{
			m_sortMode = ESortMode::Modified;
			m_filterDirty = true;
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	if (ImGui::Checkbox(Loc::Text("asset_browser.show_meta"), &m_showMetaFiles))
	{
		m_entriesDirty = true;
	}

	ImGui::EndChild();
}

void CAssetBrowserTool::DrawBrowserColumns()
{
	static float		splitRatio = 0.2f;
	constexpr float		SPLITTER_W = 3.0f;
	constexpr float		MIN_RATIO = 0.15f;
	constexpr float		MAX_RATIO = 0.80f;
	const	  ImVec2	availSpace = ImGui::GetContentRegionAvail();

	ImGui::BeginChild("AssetBrowserBody", ImVec2(0.0f, 0.0f), false);

	ImGui::BeginChild("AssetBrowserFolderTree", ImVec2(availSpace.x * splitRatio, 0.0f), true);
	if (ImGui::CollapsingHeader(Loc::Text("asset_browser.contents_folders"), ImGuiTreeNodeFlags_DefaultOpen))
	{
		DrawFolderTree();

	}
	if (ImGui::CollapsingHeader(Loc::Text("asset_browser.favorite_folders"), ImGuiTreeNodeFlags_DefaultOpen))
	{
	}
	ImGui::EndChild();

	ImGui::Utillity::VerticalSplitter("##InspSplitter", splitRatio, availSpace, MIN_RATIO, MAX_RATIO, SPLITTER_W);

	ImGui::BeginChild("AssetBrowserEntries", ImVec2(0.0f, 0.0f), true);
	DrawEntries();
	DrawBackgroundContextMenu();
	ImGui::EndChild();

	ImGui::EndChild();
}

void CAssetBrowserTool::DrawFolderTree()
{
	if (false == m_assetRootPath.empty())
	{
		DrawFolderTreeNode(m_assetRootPath);
	}
	if (false == m_scriptRootPath.empty())
	{
		DrawFolderTreeNode(m_scriptRootPath);
	}
}

void CAssetBrowserTool::DrawFolderTreeNode(const File::Path& folderPath)
{
	const bool selected = folderPath == m_focusFolderPath;
	std::string label;
	if (folderPath == m_assetRootPath)
	{
		label = "Assets";
	}
	else if (folderPath == m_scriptRootPath)
	{
		label = "Scripts";
	}
	else
	{
		label = ToUtf8(folderPath.filename());
	}
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	flags |= selected ? ImGuiTreeNodeFlags_Selected : 0;

	ImGui::PushID(ToUtf8(folderPath).c_str());
	const bool isClicked = ImTree(label.c_str(), flags);
	if (ImGui::IsItemClicked())
	{
		SetFocusFolderPath(folderPath);
	}

	if (isClicked)
	{
		auto cacheIt = m_folderChildrenCache.find(folderPath);
		if (cacheIt == m_folderChildrenCache.end())
		{
			std::vector<File::Path> childFolders;
			std::error_code errorCode;
			for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folderPath, errorCode))
			{
				if (errorCode)
				{
					errorCode.clear();
					continue; // 권한 거부 등 한 항목 실패가 폴더 전체를 막지 않도록
				}
				if (entry.is_directory(errorCode))
				{
					childFolders.emplace_back(entry.path());
				}
				errorCode.clear();
			}

			std::sort(childFolders.begin(), childFolders.end(), [](const File::Path& lhs, const File::Path& rhs) {
				return ToUtf8(lhs.filename()) < ToUtf8(rhs.filename());
			});
			cacheIt = m_folderChildrenCache.emplace(folderPath, std::move(childFolders)).first;
		}

		for (const File::Path& childFolder : cacheIt->second)
		{
			DrawFolderTreeNode(childFolder);
		}

		ImGui::TreePop();
	}
	ImGui::PopID();
}

void CAssetBrowserTool::DrawEntries()
{
	if (m_viewMode == EViewMode::List)
	{
		DrawListEntries();
	}
	else
	{
		DrawIconEntries();
	}
}

void CAssetBrowserTool::DrawListEntries()
{
	const ImGuiTableFlags flags =
		ImGuiTableFlags_BordersInnerV |
		ImGuiTableFlags_Resizable |
		ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_NoBordersInBody;
	if (false == ImGui::BeginTable("AssetBrowserList", 4, flags, ImVec2(700.0f, 0.0f)))
	{
		return;
	}

	ImGui::TableSetupColumn(Loc::Text("common.name"), ImGuiTableColumnFlags_WidthFixed, 250.0f);
	ImGui::TableSetupColumn(Loc::Text("common.type"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
	ImGui::TableSetupColumn(Loc::Text("common.modified"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
	ImGui::TableSetupColumn(Loc::Text("common.guid"), ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableHeadersRow();

	ImGuiListClipper clipper;
	clipper.Begin(static_cast<int>(m_filteredEntryIndices.size()));
	while (clipper.Step())
	{
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
		{
			AssetBrowserEntry& entry = m_entries[m_filteredEntryIndices[static_cast<std::size_t>(row)]];
			ImGui::PushID(entry.AbsolutePathUtf8.c_str()); // 캐시된 utf8 사용
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			const bool selected = entry.AbsolutePath == m_selectedEntryPath;
			const std::string label = std::format("{} {}", GetEntryIcon(entry), entry.DisplayNameUtf8);
			if (m_isRenaming && entry.AbsolutePath == m_renamingPath)
			{
				ImGui::SetKeyboardFocusHere();
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::InputText("##Rename", &m_renameBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
				{
					CommitRename(entry.AbsolutePath);
				}
				if (ImGui::IsKeyPressed(ImGuiKey_Escape))
				{
					CancelRename();
				}
			}
			else if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
			{
				SelectEntry(entry);
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					OpenEntry(entry);
				}
			}
			DrawEntryContextMenu(entry);
			BeginAssetDragDropSource(entry);

			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(entry.IsDirectory ? Loc::Text("common.folder") : GetTypeName(entry.Type));
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(entry.IsDirectory ? "-" : entry.ModifiedTimeText.c_str());
			ImGui::TableSetColumnIndex(3);
			// 캐시된 ModifiedTimeText 사용 — 매 프레임 localtime/strftime 호출 제거
			ImGui::TextUnformatted(entry.Guid.IsNull() ? "-" : ToUtf8(entry.Guid).c_str());
			ImGui::PopID();
		}
	}

	ImGui::EndTable();
}

void CAssetBrowserTool::DrawIconEntries()
{
	const ImVec2 available = ImGui::GetContentRegionAvail();
	const int columnCount = std::max(1, static_cast<int>(available.x / ICON_CELL_WIDTH));
	const int rowCount = static_cast<int>((m_filteredEntryIndices.size() + static_cast<std::size_t>(columnCount) - 1) / static_cast<std::size_t>(columnCount));

	ImGuiListClipper clipper;
	clipper.Begin(rowCount, ICON_CELL_HEIGHT);
	while (clipper.Step())
	{
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
		{
			for (int column = 0; column < columnCount; ++column)
			{
				const std::size_t index = static_cast<std::size_t>(row * columnCount + column);
				if (index >= m_filteredEntryIndices.size())
				{
					break;
				}

				AssetBrowserEntry& entry = m_entries[m_filteredEntryIndices[index]];
				ImGui::PushID(entry.AbsolutePathUtf8.c_str()); // 캐시된 utf8 사용
				const bool selected = entry.AbsolutePath == m_selectedEntryPath;
				const std::string label = std::format("{}\n{}", GetEntryIcon(entry), entry.DisplayNameUtf8);
				if (m_isRenaming && entry.AbsolutePath == m_renamingPath)
				{
					ImGui::BeginGroup();
					ImGui::TextUnformatted(GetEntryIcon(entry));
					ImGui::SetKeyboardFocusHere();
					ImGui::SetNextItemWidth(ICON_CELL_WIDTH - 8.0f);
					if (ImGui::InputText("##Rename", &m_renameBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
					{
						CommitRename(entry.AbsolutePath);
					}
					if (ImGui::IsKeyPressed(ImGuiKey_Escape))
					{
						CancelRename();
					}
					ImGui::EndGroup();
				}
				else if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(ICON_CELL_WIDTH - 8.0f, ICON_CELL_HEIGHT - 8.0f)))
				{
					SelectEntry(entry);
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						OpenEntry(entry);
					}
				}
				DrawEntryContextMenu(entry);
				BeginAssetDragDropSource(entry);
				ImGui::PopID();

				if (column + 1 < columnCount)
				{
					ImGui::SameLine();
				}
			}
		}
	}
}

void CAssetBrowserTool::DrawEntryContextMenu(const AssetBrowserEntry& entry)
{
	if (ImGui::BeginPopupContextItem(ENTRY_CONTEXT_POPUP_ID))
	{
		if (ImGui::MenuItem(Loc::Text("common.open")))
		{
			OpenEntry(entry);
		}
		if (ImGui::MenuItem(Loc::Text("asset_browser.open_in_explorer")))
		{
			QueueOperation({ EPendingOperationType::OpenInExplorer, entry.AbsolutePath, File::NULL_PATH });
		}
		if (ImGui::MenuItem(Loc::Text("asset_browser.copy_path")))
		{
			QueueOperation({ EPendingOperationType::CopyPath, entry.AbsolutePath, File::NULL_PATH });
		}
		if (ImGui::MenuItem(Loc::Text("common.rename")))
		{
			StartRename(entry);
		}
		if (ImGui::MenuItem(Loc::Text("common.delete")))
		{
			m_deleteTargetPath = entry.AbsolutePath;
			m_requestDeletePopup = true;
		}
		ImGui::EndPopup();
	}
}

void CAssetBrowserTool::DrawBackgroundContextMenu()
{
	if (ImGui::BeginPopupContextWindow(BACKGROUND_CONTEXT_POPUP_ID, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		if (ImGui::MenuItem(Loc::Text("asset_browser.create_folder")))
		{
			QueueOperation({ EPendingOperationType::CreateFolder, m_focusFolderPath / File::Path("New Folder"), File::NULL_PATH });
		}
		if (ImGui::MenuItem(Loc::Text("common.refresh")))
		{
			m_folderChildrenCache.clear();
			m_entriesDirty = true;
		}
		if (ImGui::MenuItem(Loc::Text("asset_browser.copy_folder_path")))
		{
			QueueOperation({ EPendingOperationType::CopyPath, m_focusFolderPath, File::NULL_PATH });
		}
		ImGui::Separator();
		if (ImGui::MenuItem(Loc::Text("asset_browser.open_in_explorer")))
		{
			QueueOperation({ EPendingOperationType::OpenInExplorer, m_focusFolderPath, File::NULL_PATH });
		}
		ImGui::EndPopup();
	}
}

void CAssetBrowserTool::DrawDeleteConfirmPopup()
{
	constexpr const char* POPUP_ID = "Delete Asset Entry";

	if (m_requestDeletePopup)
	{
		ImGui::OpenPopup(POPUP_ID);
		m_requestDeletePopup = false;
	}

	if (ImGui::BeginPopupModal(POPUP_ID, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted(Loc::Text("asset_browser.delete_confirm"));
		ImGui::TextUnformatted(ToUtf8(m_deleteTargetPath).c_str());
		ImGui::Separator();

		if (ImGui::Button(Loc::Text("common.delete")))
		{
			QueueOperation({ EPendingOperationType::Delete, m_deleteTargetPath, File::NULL_PATH });
			m_deleteTargetPath = File::NULL_PATH;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button(Loc::Text("common.cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			m_deleteTargetPath = File::NULL_PATH;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

void CAssetBrowserTool::SelectEntry(const AssetBrowserEntry& entry)
{
	m_selectedEntryPath = entry.AbsolutePath;
	Editor::ClearSelection();
	Editor::SelectAsset(entry.Guid, entry.AbsolutePath);
}

void CAssetBrowserTool::OpenEntry(const AssetBrowserEntry& entry)
{
	m_openDispatcher.Open(*this, entry);
}

void CAssetBrowserTool::StartRename(const AssetBrowserEntry& entry)
{
	m_isRenaming = true;
	m_renamingPath = entry.AbsolutePath;
	m_renameBuffer = entry.DisplayNameUtf8;
}

void CAssetBrowserTool::CancelRename()
{
	m_isRenaming = false;
	m_renamingPath = File::NULL_PATH;
	m_renameBuffer.clear();
}

void CAssetBrowserTool::CommitRename(const File::Path& sourcePath)
{
	if (false == m_isRenaming || m_renameBuffer.empty())
	{
		CancelRename();
		return;
	}

	File::Path targetPath = sourcePath.parent_path() / File::Path(Utillity::U8ToWString(m_renameBuffer));
	QueueOperation({ EPendingOperationType::Rename, sourcePath, targetPath });
	CancelRename();
}

void CAssetBrowserTool::QueueOperation(const PendingOperation& operation)
{
	m_pendingOperations.push_back(operation);
}

bool CAssetBrowserTool::IsProjectLoaded() const
{
	SafePtr<CProjectManager> projectManager = GetProjectManager();
	return projectManager && projectManager->IsProjectLoaded();
}

namespace
{
	// path 가 root 안(또는 root 자체) 인지 검사. lexically_relative 의 ".." 등장 여부로 판정.
	bool IsInsideRoot(const File::Path& path, const File::Path& root)
	{
		if (root.empty() || path.empty())
		{
			return false;
		}

		const std::filesystem::path relative = path.lexically_relative(root);
		if (relative.empty())
		{
			return path == root;
		}

		for (const std::filesystem::path& part : relative)
		{
			if (part == "..")
			{
				return false;
			}
		}
		return true;
	}
}

bool CAssetBrowserTool::IsInsideAssetRoot(const File::Path& path) const
{
	return IsInsideRoot(path, m_assetRootPath);
}

bool CAssetBrowserTool::IsInsideScriptRoot(const File::Path& path) const
{
	return IsInsideRoot(path, m_scriptRootPath);
}

bool CAssetBrowserTool::IsInsideAnyRoot(const File::Path& path) const
{
	return IsInsideAssetRoot(path) || IsInsideScriptRoot(path);
}

bool CAssetBrowserTool::ResolveRoot(const File::Path& path, File::Path& outRoot, File::Path& outRelative) const
{
	const File::Path* root = nullptr;
	if (IsInsideAssetRoot(path))
	{
		root = &m_assetRootPath;
	}
	else if (IsInsideScriptRoot(path))
	{
		root = &m_scriptRootPath;
	}
	if (nullptr == root)
	{
		return false;
	}

	std::error_code errorCode;
	std::filesystem::path relative = std::filesystem::relative(path, *root, errorCode);
	if (errorCode)
	{
		return false;
	}

	outRoot = *root;
	outRelative = File::Path(relative);
	return true;
}

bool CAssetBrowserTool::MakeAssetRelativePath(const File::Path& absolutePath, File::Path& outRelativePath) const
{
	if (false == IsInsideAssetRoot(absolutePath))
	{
		return false;
	}

	std::error_code errorCode;
	std::filesystem::path relativePath = std::filesystem::relative(absolutePath, m_assetRootPath, errorCode);
	if (errorCode)
	{
		return false;
	}

	outRelativePath = File::Path(relativePath);
	return true;
}

bool CAssetBrowserTool::ShouldShowPath(const File::Path& absolutePath, bool isDirectory) const
{
	if (isDirectory)
	{
		return true;
	}

	if (m_showMetaFiles)
	{
		return true;
	}

	return false == CAssetPath::IsMetaPath(absolutePath.generic_string().c_str());
}

const AssetBrowserEntry* CAssetBrowserTool::FindSelectedEntry() const
{
	for (const AssetBrowserEntry& entry : m_entries)
	{
		if (entry.AbsolutePath == m_selectedEntryPath)
		{
			return &entry;
		}
	}
	return nullptr;
}
