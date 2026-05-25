#include "pch.h"
#include "AssetBrowserTool.h"

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

	std::string ToUtf8(const std::filesystem::path& path)
	{
		const auto text = path.generic_u8string();
		return std::string(reinterpret_cast<const char*>(text.c_str()), text.size());
	}

	std::string ToLower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		return text;
	}

	std::time_t ToTimeT(std::filesystem::file_time_type fileTime)
	{
		const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
			fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
		return std::chrono::system_clock::to_time_t(systemTime);
	}

	const char* GetTypeName(EAssetType type)
	{
		switch (type)
		{
		case EAssetType::Texture: return "Texture";
		case EAssetType::Sprite: return "Sprite";
		case EAssetType::Mesh: return "Mesh";
		case EAssetType::Material: return "Material";
		case EAssetType::Shader: return "Shader";
		case EAssetType::Scene: return "Scene";
		case EAssetType::Prefab: return "Prefab";
		case EAssetType::Script: return "Script";
		case EAssetType::Custom: return "Custom";
		default: return "Unknown";
		}
	}

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

	class CDefaultAssetOpenHandler final : public CAssetBrowserTool::IAssetOpenHandler
	{
	public:
		bool CanOpen(const AssetBrowserEntry& entry) const override
		{
			return true;
		}

		void Open(CAssetBrowserTool& browser, const AssetBrowserEntry& entry) override
		{
			if (entry.IsDirectory)
			{
				browser.SetFocusFolderPath(entry.AbsolutePath);
				return;
			}

			File::OpenFile(entry.AbsolutePath);
		}
	};

	class CSceneAssetOpenHandler final : public CAssetBrowserTool::IAssetOpenHandler
	{
	public:
		bool CanOpen(const AssetBrowserEntry& entry) const override
		{
			return false == entry.IsDirectory
				&& (EAssetType::Scene == entry.Type || entry.ExtensionUtf8 == ".jscene");
		}

		void Open(CAssetBrowserTool&, const AssetBrowserEntry& entry) override
		{
			if (false == Core::SceneManager.IsValid())
			{
				CSystemLog::Error(Utillity::U8(u8"씬 로드에 실패하였습니다."));
				File::OpenFile(entry.AbsolutePath);
				return;
			}

			const std::string sceneName = entry.RelativePath.empty() ? entry.DisplayNameUtf8 : ToUtf8(entry.RelativePath);
			CScene* scene = Core::SceneManager->CreateScene(sceneName.c_str());
			if (nullptr == scene)
			{
				CSystemLog::Error(Utillity::U8(u8"씬 로드에 실패하였습니다."));
				return;
			}

			CSceneSerializer serializer;
			const std::string path = entry.AbsolutePath.string();
			if (ESceneSerializeResult::Success == serializer.LoadFromFile(*scene, path.c_str()))
			{
				if (const EngineContext* context = Editor::ImEditor ? Editor::ImEditor->GetEditorEngineContext() : nullptr)
				{
					CSpriteRenderSystem* spriteSystem = scene->FindSystem<CSpriteRenderSystem>();
					if (nullptr == spriteSystem)
					{
						spriteSystem = scene->AddSystem<CSpriteRenderSystem>(context->RenderScene.TryGet());
					}
					if (nullptr != spriteSystem)
					{
						spriteSystem->SetRenderScene(context->RenderScene.TryGet());
						spriteSystem->SetDependencies(context->AssetManager.TryGet(), context->RHIDevice.TryGet(), context->Renderer.TryGet());
					}
				}
				Core::SceneManager->SetActiveScene(sceneName.c_str());
				Editor::SetActiveScenePath(entry.AbsolutePath);
				Editor::CommandManager.SetActiveDocument(sceneName.c_str());
				Editor::CommandManager.MarkSaved(sceneName.c_str());
				CSystemLog::Info("Scene loaded.");
			}
			else
			{
				CSystemLog::Error(Utillity::U8(u8"씬 로드에 실패하였습니다."));
			}
		}
	};

	SafePtr<CProjectManager> GetProjectManager()
	{
		return Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
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
	SetTitle("Asset Browser");
	m_openDispatcher.RegisterHandler(MakeOwnerPtr<CSceneAssetOpenHandler>());
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
	if (false == IsInsideAssetRoot(path))
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
		if (false == m_assetRootPath.empty())
		{
			ResetProjectState();
		}
		return;
	}

	const File::Path& assetPath = projectManager->GetAssetPath();
	if (m_assetRootPath != assetPath)
	{
		ResetProjectState();
		m_assetRootPath = assetPath;
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
		if (errorCode)
		{
			errorCode.clear();
			break;
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

		AssetBrowserEntry browserEntry;
		browserEntry.AbsolutePath = absolutePath;
		browserEntry.IsDirectory = isDirectory;
		browserEntry.IsImportable = false == isDirectory && false == CAssetPath::IsMetaPath(absolutePath.generic_string().c_str());
		MakeAssetRelativePath(absolutePath, browserEntry.RelativePath);
		browserEntry.DisplayNameUtf8 = ToUtf8(absolutePath.filename());
		browserEntry.ExtensionUtf8 = ToLower(ToUtf8(absolutePath.extension()));

		if (false == isDirectory)
		{
			const std::filesystem::file_time_type lastWriteTime = entry.last_write_time(errorCode);
			if (false == static_cast<bool>(errorCode))
			{
				browserEntry.LastWriteTime = ToTimeT(lastWriteTime);
			}
			errorCode.clear();

			if (registry)
			{
				const std::string relativePath = browserEntry.RelativePath.generic_string();
				if (const AssetMetaData* metaData = registry->FindAssetByPath(relativePath.c_str()))
				{
					browserEntry.Guid = metaData->Guid;
					browserEntry.Type = metaData->Type;
				}
			}

			const std::string metaRelativePath = CAssetPath::MakeMetaPath(browserEntry.RelativePath.generic_string().c_str());
			browserEntry.HasMeta = std::filesystem::exists(m_assetRootPath / File::Path(metaRelativePath), errorCode);
			errorCode.clear();
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
		if (false == filter.empty() && ToLower(m_entries[i].DisplayNameUtf8).find(filter) == std::string::npos)
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
		if (false == IsInsideAssetRoot(operation.Path))
		{
			continue;
		}

		std::error_code errorCode;
		switch (operation.Type)
		{
		case EPendingOperationType::CreateFolder:
			Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CCreateFolderCommand>(operation.Path), "__AssetDatabase");
			break;
		case EPendingOperationType::Rename:
			if (IsInsideAssetRoot(operation.TargetPath))
			{
				Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CRenamePathCommand>(operation.Path, operation.TargetPath, m_assetRootPath), "__AssetDatabase");
			}
			break;
		case EPendingOperationType::Delete:
			Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CDeletePathCommand>(operation.Path, m_assetRootPath), "__AssetDatabase");
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
	ImGui::TextDisabled("No project loaded.");
	ImGui::TextDisabled("Use File > Open Project to load a .Jproject file.");
}

void CAssetBrowserTool::DrawToolbar()
{
	ImGui::BeginChild("AssetBrowserToolbar", ImVec2(0.0f, TOOLBAR_HEIGHT), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	const bool canBack = false == m_backStack.empty();
	const bool canForward = false == m_forwardStack.empty();
	const bool canUp = m_focusFolderPath != m_assetRootPath && false == m_focusFolderPath.empty();

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
	if (ImGui::Button("Up"))
	{
		SetFocusFolderPath(File::Path(m_focusFolderPath.parent_path()));
	}
	if (false == canUp) ImGui::EndDisabled();

	ImGui::SameLine();
	if (ImGui::Button("Refresh"))
	{
		m_folderChildrenCache.clear();
		m_entriesDirty = true;
	}

	ImGui::SameLine();
	const File::Path relativeFocusPath = m_focusFolderPath == m_assetRootPath ? File::Path("Assets") : File::Path("Assets") / m_focusFolderPath.lexically_relative(m_assetRootPath);
	ImGui::TextUnformatted(ToUtf8(relativeFocusPath).c_str());

	ImGui::SameLine();
	ImGui::SetNextItemWidth(180.0f);
	if (ImGui::InputTextWithHint("##AssetBrowserSearch", "Search", &m_searchText))
	{
		m_filterDirty = true;
	}

	ImGui::SameLine();
	if (ImGui::BeginCombo("##AssetBrowserViewMode", m_viewMode == EViewMode::List ? "List" : "Icon"))
	{
		if (ImGui::Selectable("List", m_viewMode == EViewMode::List))
		{
			m_viewMode = EViewMode::List;
		}
		if (ImGui::Selectable("Icon", m_viewMode == EViewMode::Icon))
		{
			m_viewMode = EViewMode::Icon;
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	const char* sortLabel = m_sortMode == ESortMode::Name ? "Name" : (m_sortMode == ESortMode::Type ? "Type" : "Modified");
	if (ImGui::BeginCombo("##AssetBrowserSortMode", sortLabel))
	{
		if (ImGui::Selectable("Name", m_sortMode == ESortMode::Name))
		{
			m_sortMode = ESortMode::Name;
			m_filterDirty = true;
		}
		if (ImGui::Selectable("Type", m_sortMode == ESortMode::Type))
		{
			m_sortMode = ESortMode::Type;
			m_filterDirty = true;
		}
		if (ImGui::Selectable("Modified", m_sortMode == ESortMode::Modified))
		{
			m_sortMode = ESortMode::Modified;
			m_filterDirty = true;
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	if (ImGui::Checkbox("Show .Jmeta", &m_showMetaFiles))
	{
		m_entriesDirty = true;
	}

	ImGui::EndChild();
}

void CAssetBrowserTool::DrawBrowserColumns()
{
	ImGui::BeginChild("AssetBrowserBody", ImVec2(0.0f, 0.0f), false);
	ImGui::BeginChild("AssetBrowserFolderTree", ImVec2(FOLDER_PANEL_WIDTH, 0.0f), true);
	DrawFolderTree();
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("AssetBrowserEntries", ImVec2(0.0f, 0.0f), true);
	DrawEntries();
	DrawBackgroundContextMenu();
	ImGui::EndChild();
	ImGui::EndChild();
}

void CAssetBrowserTool::DrawFolderTree()
{
	if (m_assetRootPath.empty())
	{
		return;
	}

	DrawFolderTreeNode(m_assetRootPath);
}

void CAssetBrowserTool::DrawFolderTreeNode(const File::Path& folderPath)
{
	const bool selected = folderPath == m_focusFolderPath;
	const std::string label = folderPath == m_assetRootPath ? "Assets" : ToUtf8(folderPath.filename());
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	flags |= selected ? ImGuiTreeNodeFlags_Selected : 0;

	ImGui::PushID(ToUtf8(folderPath).c_str());
	const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		SetFocusFolderPath(folderPath);
	}

	if (open)
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
					break;
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
	const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
	if (false == ImGui::BeginTable("AssetBrowserList", 4, flags, ImVec2(0.0f, 0.0f)))
	{
		return;
	}

	ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
	ImGui::TableSetupColumn("Guid", ImGuiTableColumnFlags_WidthFixed, 220.0f);
	ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 150.0f);
	ImGui::TableHeadersRow();

	ImGuiListClipper clipper;
	clipper.Begin(static_cast<int>(m_filteredEntryIndices.size()));
	while (clipper.Step())
	{
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
		{
			AssetBrowserEntry& entry = m_entries[m_filteredEntryIndices[static_cast<std::size_t>(row)]];
			ImGui::PushID(ToUtf8(entry.AbsolutePath).c_str());
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
			ImGui::TextUnformatted(entry.IsDirectory ? "Folder" : GetTypeName(entry.Type));
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(entry.Guid.IsNull() ? "-" : ToUtf8(entry.Guid).c_str());
			ImGui::TableSetColumnIndex(3);
			if (entry.IsDirectory)
			{
				ImGui::TextUnformatted("-");
			}
			else
			{
				std::tm timeInfo = {};
				localtime_s(&timeInfo, &entry.LastWriteTime);
				char buffer[64] = {};
				std::strftime(buffer, sizeof(buffer), "%F %T", &timeInfo);
				ImGui::TextUnformatted(buffer);
			}
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
				ImGui::PushID(ToUtf8(entry.AbsolutePath).c_str());
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
		if (ImGui::MenuItem("Open"))
		{
			OpenEntry(entry);
		}
		if (ImGui::MenuItem("Open In Explorer"))
		{
			QueueOperation({ EPendingOperationType::OpenInExplorer, entry.AbsolutePath, File::NULL_PATH });
		}
		if (ImGui::MenuItem("Copy Path"))
		{
			QueueOperation({ EPendingOperationType::CopyPath, entry.AbsolutePath, File::NULL_PATH });
		}
		if (ImGui::MenuItem("Rename"))
		{
			StartRename(entry);
		}
		if (ImGui::MenuItem("Delete"))
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
		if (ImGui::MenuItem("Create Folder"))
		{
			QueueOperation({ EPendingOperationType::CreateFolder, m_focusFolderPath / File::Path("New Folder"), File::NULL_PATH });
		}
		if (ImGui::MenuItem("Refresh"))
		{
			m_folderChildrenCache.clear();
			m_entriesDirty = true;
		}
		if (ImGui::MenuItem("Copy Folder Path"))
		{
			QueueOperation({ EPendingOperationType::CopyPath, m_focusFolderPath, File::NULL_PATH });
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Open In Explorer"))
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
		ImGui::TextUnformatted("Delete selected asset entry?");
		ImGui::TextUnformatted(ToUtf8(m_deleteTargetPath).c_str());
		ImGui::Separator();

		if (ImGui::Button("Delete"))
		{
			QueueOperation({ EPendingOperationType::Delete, m_deleteTargetPath, File::NULL_PATH });
			m_deleteTargetPath = File::NULL_PATH;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
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

bool CAssetBrowserTool::IsInsideAssetRoot(const File::Path& path) const
{
	if (m_assetRootPath.empty() || path.empty())
	{
		return false;
	}

	const std::filesystem::path relative = path.lexically_relative(m_assetRootPath);
	if (relative.empty())
	{
		return path == m_assetRootPath;
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
