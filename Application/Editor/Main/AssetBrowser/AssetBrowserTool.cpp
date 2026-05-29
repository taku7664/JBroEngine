#include "pch.h"
#include "AssetBrowserTool.h"
#include "AssetHandler.h"

#include "Editor/Editor.h"
#include "Editor/Command/EditorFileCommands.h"
#include "Editor/EditorDragDrop.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/Core/Core.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Core/Asset/AssetPath.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/IAssetRegistry.h"
#include "Engine/Core/Asset/SpriteAsset.h"
#include "Engine/Core/Resource/ResourceRegistry.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/IRHITexture.h"
#include "Engine/Editor/ImGuiUtillity.h"
#include "Engine/Editor/ImWindow/ImWindowContext.h"
#include "Engine/Editor/ImWindow/IImPopupWindow.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "Engine/GameFramework/Rendering/SpriteRenderSystem.h"
#include "File/FileUtillities.h"
#include "StringUtillity.h"

#include <array>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <memory>
#include <sstream>

namespace
{
	constexpr float TOOLBAR_HEIGHT = 32.0f;
	constexpr float FOLDER_PANEL_WIDTH = 260.0f;
	constexpr float ICON_CELL_WIDTH = 112.0f;
	constexpr float ICON_CELL_HEIGHT = 92.0f;
	// SceneView 의 ##SVCtxMenu 와 동일한 패턴: 단일 ID 로 모든 우클릭 통합.
	constexpr const char* ASSET_BROWSER_CTX_POPUP_ID = "##asset_browser_popup";

	// 같은 폴더에 같은 이름이 있을 경우 BaseName, BaseName2, BaseName3 ...
	File::Path MakeUniqueFilePath(const File::Path& parentFolder,
	                              const std::string& baseName,
	                              const std::string& ext)
	{
		std::error_code ec;
		for (int i = 0; i < 10000; ++i)
		{
			const std::string name = (0 == i) ? baseName : baseName + std::to_string(i + 1);
			File::Path candidate = parentFolder / File::Path(name + ext);
			if (false == std::filesystem::exists(candidate, ec)) return candidate;
			ec.clear();
		}
		return File::Path();
	}

	bool WriteTextFile(const File::Path& path, std::string_view content)
	{
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
		if (false == file.is_open()) return false;
		file.write(content.data(), static_cast<std::streamsize>(content.size()));
		return true;
	}

	bool CopyFileOnce(const File::Path& src, const File::Path& dst)
	{
		std::error_code ec;
		std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
		return false == static_cast<bool>(ec);
	}

	// ── 신규 자산 템플릿 ────────────────────────────────────────────────────
	// AssetWatcher 가 새 파일 감지 후 자동으로 ImportAsset 을 호출하므로
	// 우리는 디스크에 텍스트 파일만 작성하면 된다.
	constexpr std::string_view EMPTY_SCENE_YAML =
	    "Scene:\n"
	    "  Entities: []\n";
	constexpr std::string_view EMPTY_MATERIAL_YAML =
	    "Material:\n"
	    "  Shader: \"\"\n"
	    "  Properties: {}\n";
	constexpr std::string_view EMPTY_PREFAB_YAML =
	    "Prefab:\n"
	    "  Root: 0\n"
	    "  Entities: []\n";

	// ── 스크립트 추가 팝업의 입력 상태 ─────────────────────────────────────
	struct NewScriptProperty
	{
		int         TypeIndex = 3;   // 기본값 = float
		std::string Name      = "";
	};
	struct NewScriptInput
	{
		std::string                    ClassName    = "NewScript";
		std::vector<NewScriptProperty> Properties;
		File::Path                     ParentFolder;
	};

	// ScriptMacros.h 가 지원하는 REFLECT_FIELD 타입 목록.
	// Combo 의 표시 라벨 ↔ C++ 타입 토큰.
	constexpr std::array<std::pair<const char*, const char*>, 4> SCRIPT_PROP_TYPES = {{
		{ "bool",     "bool"            },
		{ "int32",    "std::int32_t"    },
		{ "uint32",   "std::uint32_t"   },
		{ "float",    "float"           },
	}};

	std::string DefaultValueFor(int typeIndex)
	{
		switch (typeIndex)
		{
		case 0: return "false";
		case 1: return "0";
		case 2: return "0u";
		case 3: return "0.0f";
		default: return "{}";
		}
	}

	std::string MakeScriptHeader(const std::string& className,
	                             const std::vector<NewScriptProperty>& props)
	{
		std::ostringstream out;
		out << "#pragma once\n\n";
		out << "#include \"GameFramework/Scripting/ScriptAPI.h\"\n\n";
		out << "JBRO_SCRIPT " << className << " final : public CGameScript\n";
		out << "{\n";
		out << "\tSCRIPT_CLASS(" << className << ")\n";
		if (false == props.empty())
		{
			out << "\n";
			for (const NewScriptProperty& p : props)
			{
				if (p.TypeIndex < 0 || p.TypeIndex >= static_cast<int>(SCRIPT_PROP_TYPES.size())) continue;
				const char* typeToken = SCRIPT_PROP_TYPES[p.TypeIndex].second;
				out << "\tREFLECT_FIELD(" << typeToken << ", "
				    << p.Name << ", " << DefaultValueFor(p.TypeIndex) << ")\n";
			}
		}
		out << "\nprotected:\n";
		out << "\tvoid OnCreate() override;\n";
		out << "\tvoid OnStart() override;\n";
		out << "\tvoid OnUpdate() override;\n";
		out << "\tvoid OnFixedUpdate() override;\n";
		out << "\tvoid OnDestroy() override;\n";
		out << "};\n";
		return out.str();
	}
	std::string MakeScriptSource(const std::string& className, const std::string& headerFile)
	{
		std::ostringstream out;
		out << "#include \"pch.h\"\n";
		out << "#include \"" << headerFile << "\"\n\n";
		out << "void " << className << "::OnCreate()      {}\n";
		out << "void " << className << "::OnStart()       {}\n";
		out << "void " << className << "::OnUpdate()      {}\n";
		out << "void " << className << "::OnFixedUpdate() {}\n";
		out << "void " << className << "::OnDestroy()     {}\n";
		return out.str();
	}

	// 클래스명 + 부모 폴더 → 새 .h / .cpp 페어 경로(충돌 회피).
	// 이름이 이미 점유되어 있으면 NameN 으로 증가.
	bool ResolveScriptPaths(const File::Path& parentFolder,
	                        const std::string& className,
	                        File::Path& outH, File::Path& outCpp)
	{
		std::error_code ec;
		for (int i = 0; i < 10000; ++i)
		{
			const std::string baseName = (0 == i) ? className : className + std::to_string(i + 1);
			File::Path h   = parentFolder / File::Path(baseName + ".h");
			File::Path cpp = parentFolder / File::Path(baseName + ".cpp");
			if (false == std::filesystem::exists(h, ec)
			    && false == std::filesystem::exists(cpp, ec))
			{
				outH = std::move(h);
				outCpp = std::move(cpp);
				return true;
			}
			ec.clear();
		}
		return false;
	}

	// std::string 버퍼용 InputText + invalid 시 빨간 프레임 외곽선.
	// (ImGui::Utillity::ValidatedInputText 가 제거된 뒤의 로컬 대체.)
	bool ValidatedStringInput(const char* id, std::string* buffer, bool invalid,
	                          ImGuiInputTextFlags flags = 0)
	{
		if (invalid)
		{
			ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
		}
		const bool changed = ImGui::InputText(id, buffer, flags);
		if (invalid)
		{
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
		}
		return changed;
	}

	const char* GetEntryIcon(const AssetBrowserEntry& entry)
	{
		if (entry.IsDirectory)
		{
			return "[DIR]";
		}

		switch (entry.Type)
		{
		case EAssetType::Sprite: return "[SPR]";
		case EAssetType::Material: return "[MAT]";
		case EAssetType::Scene: return "[SCN]";
		case EAssetType::Prefab: return "[PFB]";
		case EAssetType::Script: return "[SCR]";
		default: return "[FILE]";
		}
	}

	// entry 타입에 대응하는 ResourceRegistry 아이콘 키.
	// 이미지 확장자는 Thumbnail 에 실제 자산을 직접 로드하므로 여기서 처리하지 않는다.
	const char* GetIconResourceKey(const AssetBrowserEntry& entry)
	{
		if (entry.IsDirectory) return "icon-folder";
		switch (entry.Type)
		{
		case EAssetType::Scene:    return "icon-scene";
		case EAssetType::Script:   return "icon-script";
		case EAssetType::Material: return "icon-material";
		case EAssetType::Prefab:   return "icon-object";
		default:                   return "icon-file-default";
		}
	}

	bool IsImageExtension(const std::string& ext)
	{
		return ext == ".png" || ext == ".jpg" || ext == ".jpeg"
		    || ext == ".bmp" || ext == ".tga";
	}

	// SpriteAsset 의 GPU 텍스처 핸들 → ImTextureID.  GPU 텍스처 없으면 0 반환.
	ImTextureID GetSpriteImTexture(const SafePtr<CSpriteAsset>& sprite)
	{
		if (false == sprite.IsValid())                  return 0;
		SafePtr<IRHITexture> tex = sprite->GetGpuTexture();
		if (false == tex.IsValid())                     return 0;
		void* srv = tex->GetNativeHandle().ShaderResourceView;
		return reinterpret_cast<ImTextureID>(srv);
	}

	// entry 에 SafePtr<CSpriteAsset> 썸네일을 채워둔다 (RefreshCurrentFolderEntries 에서 1회).
	// - 폴더/씬/스크립트 등: ResourceRegistry 의 아이콘
	// - 이미지 파일      : 해당 파일 자체를 path-based persistent 자산으로 로드
	void PopulateEntryThumbnail(AssetBrowserEntry& entry, SafePtr<IAssetManager> assetManager)
	{
		if (false == Core::ResourceRegistry.IsValid())
		{
			return;
		}

		if (false == entry.IsDirectory && IsImageExtension(entry.ExtensionUtf8))
		{
			// 이미지면 해당 파일의 SpriteAsset 을 직접 로드해 프리뷰로 쓴다.
			// .Jmeta 없이도 path 기반 등록 가능하도록 RegisterAssetByPath 사용 — persistent 가
			// 아닌 일반 등록(false)으로 두어 프로젝트 닫힘 시 함께 내려가게 한다.
			if (assetManager.IsValid())
			{
				assetManager->RegisterAssetByPath(entry.AbsolutePath, EAssetType::Sprite, /*isPersistent*/ false);
				SafePtr<IAsset> asset = assetManager->LoadAssetByPath(entry.AbsolutePath);
				if (asset.IsValid() && EAssetType::Sprite == asset->GetAssetType())
				{
					entry.Thumbnail = DynamicSafePtrCast<CSpriteAsset>(asset);
				}
			}
		}

		if (false == entry.Thumbnail.IsValid())
		{
			entry.Thumbnail = Core::ResourceRegistry->GetSprite(GetIconResourceKey(entry));
		}

		// GPU 텍스처가 아직 없으면 lazy 생성.
		if (entry.Thumbnail.IsValid() && Engine.RHIDevice.IsValid())
		{
			entry.Thumbnail->EnsureGpuTexture(*Engine.RHIDevice);
		}
	}

	void BeginAssetDragDropSource(const AssetBrowserEntry& entry)
	{
		EditorDragDrop::AssetPayloadDesc desc;
		desc.Guid              = entry.Guid;
		desc.RelativePath      = entry.RelativePath;
		desc.Type              = entry.Type;
		desc.IsDirectory       = entry.IsDirectory;
		desc.PreviewLabel      = entry.DisplayNameUtf8.c_str();
		desc.PreviewTextureID  = GetSpriteImTexture(entry.Thumbnail);
		desc.PreviewSize       = 56.0f;
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

	// 자동 컴파일 실패 폴링 — 새 실패 메시지가 있으면 모달 팝업으로 표시.
	if (SafePtr<CProjectManager> pm = GetProjectManager())
	{
		std::string failureMessage = pm->ConsumeLastLiveCompileFailure();
		if (false == failureMessage.empty())
		{
			ShowScriptCompileFailurePopup(std::move(failureMessage));
		}
	}
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
				if (const AssetMetaData* metaData = registry->FindAssetByPath(File::Path(relativePath)))
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

		PopulateEntryThumbnail(browserEntry, assetManager);
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

		case EPendingOperationType::Duplicate:
		{
			// 원본 파일만 복제. .Jmeta 는 복사하지 않는다 — AssetWatcher 가
			// 새 파일을 감지하면 새 GUID 로 자동 import 한다.
			const std::filesystem::path srcPath(operation.Path);
			if (false == std::filesystem::is_regular_file(srcPath, errorCode))
			{
				errorCode.clear();
				break;
			}
			errorCode.clear();
			const std::string stem = ToUtf8(srcPath.stem());
			const std::string ext  = srcPath.has_extension() ? srcPath.extension().generic_string() : std::string{};
			File::Path dst = MakeUniqueFilePath(File::Path(srcPath.parent_path()), stem + " Copy", ext);
			if (false == dst.empty() && CopyFileOnce(operation.Path, dst))
			{
				StartRenameForNewPath(dst);
			}
			break;
		}

		case EPendingOperationType::CreateScene:
		{
			if (false == insideAssetRoot) break;
			File::Path dst = MakeUniqueFilePath(operation.Path, "NewScene", ".jscene");
			if (false == dst.empty() && WriteTextFile(dst, EMPTY_SCENE_YAML))
			{
				StartRenameForNewPath(dst);
			}
			break;
		}
		case EPendingOperationType::CreateMaterial:
		{
			if (false == insideAssetRoot) break;
			File::Path dst = MakeUniqueFilePath(operation.Path, "NewMaterial", ".jmat");
			if (false == dst.empty() && WriteTextFile(dst, EMPTY_MATERIAL_YAML))
			{
				StartRenameForNewPath(dst);
			}
			break;
		}
		case EPendingOperationType::CreatePrefab:
		{
			if (false == insideAssetRoot) break;
			File::Path dst = MakeUniqueFilePath(operation.Path, "NewPrefab", ".jprefab");
			if (false == dst.empty() && WriteTextFile(dst, EMPTY_PREFAB_YAML))
			{
				StartRenameForNewPath(dst);
			}
			break;
		}
		case EPendingOperationType::CreateScript:
		{
			// Script root 안에서만 의미 있음. 파일을 바로 만들지 않고
			// 클래스 이름 + 프로퍼티 입력을 받는 모달 팝업을 띄운다.
			if (false == IsInsideScriptRoot(operation.Path)) break;
			ShowNewScriptPopup(operation.Path);
			break;
		}

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
	// 빈 공간 우클릭(다른 entry 위에서가 아닐 때) — body 통합 팝업 호출.
	// ChildWindows 플래그가 없으면 리스트 모드의 ScrollY 테이블이 내부 child 를
	// 만들어 부모 윈도우 hover 판정에서 누락된다.
	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup
	                         | ImGuiHoveredFlags_ChildWindows)
	    && false == ImGui::IsAnyItemHovered()
	    && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		OpenBodyContextMenuForBackground();
	}
	DrawBrowserBodyContextMenu();
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
	DrawFolderTreeContextMenu();
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
	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		OpenFolderTreeContextMenu(folderPath);
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
			const ImTextureID iconTex = GetSpriteImTexture(entry.Thumbnail);
			const float       lineH   = ImGui::GetTextLineHeight();
			if (0 != iconTex)
			{
				ImGui::Image(iconTex, ImVec2(lineH, lineH));
				ImGui::SameLine();
			}
			if (m_isRenaming && entry.AbsolutePath == m_renamingPath)
			{
				ImGui::SetKeyboardFocusHere();
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ValidatedStringInput("##Rename", &m_renameBuffer,
				        /*invalid*/ m_renameBuffer.empty(),
				        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
				{
					CommitRename(entry.AbsolutePath);
				}
				if (ImGui::IsKeyPressed(ImGuiKey_Escape))
				{
					CancelRename();
				}
			}
			else
			{
				const std::string label = (0 != iconTex)
					? entry.DisplayNameUtf8
					: std::format("{} {}", GetEntryIcon(entry), entry.DisplayNameUtf8);
				if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
				{
					SelectEntry(entry);
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						OpenEntry(entry);
					}
				}
			}
			// 우클릭: 통합 컨텍스트 메뉴 호출(SceneView 와 동일 패턴)
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				OpenBodyContextMenuForEntry(entry);
			}
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
				const ImTextureID iconTex   = GetSpriteImTexture(entry.Thumbnail);
				const float       cellW     = ICON_CELL_WIDTH  - 8.0f;
				const float       cellH     = ICON_CELL_HEIGHT - 8.0f;
				const float       imageSize = 56.0f;
				const ImVec2      cursor    = ImGui::GetCursorScreenPos();

				if (m_isRenaming && entry.AbsolutePath == m_renamingPath)
				{
					ImGui::BeginGroup();
					if (0 != iconTex)
					{
						const float padX = (cellW - imageSize) * 0.5f;
						ImGui::Dummy(ImVec2(padX, 0.0f));
						ImGui::SameLine();
						ImGui::Image(iconTex, ImVec2(imageSize, imageSize));
					}
					else
					{
						ImGui::TextUnformatted(GetEntryIcon(entry));
					}
					ImGui::SetKeyboardFocusHere();
					ImGui::SetNextItemWidth(cellW);
					if (ValidatedStringInput("##Rename", &m_renameBuffer,
				        /*invalid*/ m_renameBuffer.empty(),
				        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
					{
						CommitRename(entry.AbsolutePath);
					}
					if (ImGui::IsKeyPressed(ImGuiKey_Escape))
					{
						CancelRename();
					}
					ImGui::EndGroup();
				}
				else
				{
					// 셀 전체를 덮는 Selectable 로 클릭/더블클릭 처리한 뒤,
					// 같은 영역 위에 이미지+이름을 덮어 그린다.
					if (ImGui::Selectable("##cell", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(cellW, cellH)))
					{
						SelectEntry(entry);
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
						{
							OpenEntry(entry);
						}
					}
					if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
					{
						OpenBodyContextMenuForEntry(entry);
					}
					BeginAssetDragDropSource(entry);

					ImDrawList* draw = ImGui::GetWindowDrawList();
					const float padX = (cellW - imageSize) * 0.5f;
					if (0 != iconTex)
					{
						const ImVec2 imgMin(cursor.x + padX, cursor.y + 4.0f);
						const ImVec2 imgMax(imgMin.x + imageSize, imgMin.y + imageSize);
						draw->AddImage(iconTex, imgMin, imgMax);
					}
					else
					{
						draw->AddText(ImVec2(cursor.x + 4.0f, cursor.y + 4.0f),
						              ImGui::GetColorU32(ImGuiCol_Text),
						              GetEntryIcon(entry));
					}
					// 이름은 이미지 아래.
					const ImVec2 textSize = ImGui::CalcTextSize(entry.DisplayNameUtf8.c_str(), nullptr, false, cellW);
					const ImVec2 textPos(cursor.x + (cellW - std::min(textSize.x, cellW)) * 0.5f,
					                     cursor.y + 4.0f + imageSize + 2.0f);
					draw->AddText(nullptr, 0.0f,
					              textPos, ImGui::GetColorU32(ImGuiCol_Text),
					              entry.DisplayNameUtf8.c_str(), nullptr,
					              cellW);

					// Selectable 이후의 컨텍스트/드래그소스 위 블록에서 이미 처리. 아래 fall-through 막기.
					ImGui::PopID();
					if (column + 1 < columnCount)
					{
						ImGui::SameLine();
					}
					continue;
				}
				// 여기 도달하는 분기는 rename 중인 entry — 우클릭 메뉴/드래그 X.
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

// ── 컨텍스트 메뉴 열기 헬퍼 ─────────────────────────────────────────────────
// OpenPopup 은 호출 프레임 끝에 적용되며 같은 프레임의 IsMouseClicked 와
// 충돌하기 쉽다. 따라서 우클릭 감지 → 상태/플래그만 세팅, 실제 OpenPopup 은
// 각 DrawXxxContextMenu 의 첫머리에서 일괄 실행한다.
void CAssetBrowserTool::OpenBodyContextMenuForEntry(const AssetBrowserEntry& entry)
{
	SelectEntry(entry);
	m_bodyCtxEntryPath     = entry.AbsolutePath;
	m_bodyCtxOpenRequested = true;
}

void CAssetBrowserTool::OpenBodyContextMenuForBackground()
{
	m_bodyCtxEntryPath     = File::NULL_PATH;
	m_bodyCtxOpenRequested = true;
}

void CAssetBrowserTool::OpenFolderTreeContextMenu(const File::Path& folderPath)
{
	m_treeCtxFolderPath    = folderPath;
	m_treeCtxOpenRequested = true;
}

// ── 헬퍼: 폴더의 루트가 Scripts 인지 / Assets 인지 ──────────────────────────
namespace
{
	enum class ECtxRootKind { Unknown, Assets, Scripts };
}

static ECtxRootKind ClassifyRoot(const File::Path& folderPath,
                                 const File::Path& assetRoot,
                                 const File::Path& scriptRoot)
{
	auto isInside = [](const File::Path& p, const File::Path& root) -> bool
	{
		if (root.empty()) return false;
		std::error_code ec;
		auto rel = std::filesystem::relative(p, root, ec);
		if (ec) return false;
		const std::string s = rel.generic_string();
		return false == s.empty() && s != ".." && s.compare(0, 3, "../") != 0;
	};
	if (false == assetRoot.empty()  && (folderPath == assetRoot  || isInside(folderPath, assetRoot)))  return ECtxRootKind::Assets;
	if (false == scriptRoot.empty() && (folderPath == scriptRoot || isInside(folderPath, scriptRoot))) return ECtxRootKind::Scripts;
	return ECtxRootKind::Unknown;
}

// ── DrawBrowserBodyContextMenu ──────────────────────────────────────────────
// 단일 팝업 ID(##asset_browser_body_popup). 우클릭 의도가 entry 인지 빈공간인지에
// 따라 섹션 분기. 빈공간 + 현재 focus 폴더의 루트가 Scripts/Assets 에 따라
// 추가 항목("스크립트 추가" / "에셋 추가 ▶") 노출.
void CAssetBrowserTool::DrawBrowserBodyContextMenu()
{
	constexpr const char* POPUP_ID = "##asset_browser_body_popup";

	if (m_bodyCtxOpenRequested)
	{
		ImGui::OpenPopup(POPUP_ID);
		m_bodyCtxOpenRequested = false;
	}

	if (false == ImGui::BeginPopup(POPUP_ID))
	{
		return;
	}

	const bool hasEntry = false == m_bodyCtxEntryPath.empty();

	// ── 섹션 1: Entry 우클릭 ────────────────────────────────────────────────
	if (hasEntry)
	{
		// 우클릭 대상이 현재 m_entries 안에 있는지 (rename/refresh 등으로 사라졌을 가능성)
		const AssetBrowserEntry* targetEntry = nullptr;
		for (const AssetBrowserEntry& e : m_entries)
		{
			if (e.AbsolutePath == m_bodyCtxEntryPath) { targetEntry = &e; break; }
		}

		if (targetEntry)
		{
			if (ImGui::MenuItem(Loc::Text("common.open")))
			{
				OpenEntry(*targetEntry);
			}
			if (ImGui::MenuItem(Loc::Text("asset_browser.open_in_explorer")))
			{
				QueueOperation({ EPendingOperationType::OpenInExplorer, targetEntry->AbsolutePath, File::NULL_PATH });
			}
			if (ImGui::MenuItem(Loc::Text("asset_browser.copy_path")))
			{
				QueueOperation({ EPendingOperationType::CopyPath, targetEntry->AbsolutePath, File::NULL_PATH });
			}
			ImGui::Separator();
			if (ImGui::MenuItem(Loc::Text("common.rename")))
			{
				StartRename(*targetEntry);
			}
			if (false == targetEntry->IsDirectory
			    && ImGui::MenuItem(Loc::Text("common.duplicate")))
			{
				QueueOperation({ EPendingOperationType::Duplicate, targetEntry->AbsolutePath, File::NULL_PATH });
			}
			if (ImGui::MenuItem(Loc::Text("common.delete")))
			{
				m_deleteTargetPath = targetEntry->AbsolutePath;
				m_requestDeletePopup = true;
			}
		}
	}
	// ── 섹션 2: 빈공간 우클릭 ───────────────────────────────────────────────
	else
	{
		const ECtxRootKind rootKind = ClassifyRoot(m_focusFolderPath, m_assetRootPath, m_scriptRootPath);

		if (ECtxRootKind::Scripts == rootKind)
		{
			if (ImGui::MenuItem(Loc::Text("asset_browser.add_script")))
			{
				QueueOperation({ EPendingOperationType::CreateScript, m_focusFolderPath, File::NULL_PATH });
			}
			ImGui::Separator();
		}
		else if (ECtxRootKind::Assets == rootKind)
		{
			if (ImGui::BeginMenu(Loc::Text("asset_browser.add_asset")))
			{
				if (ImGui::MenuItem(Loc::Text("asset_browser.add_asset.scene")))
				{
					QueueOperation({ EPendingOperationType::CreateScene, m_focusFolderPath, File::NULL_PATH });
				}
				if (ImGui::MenuItem(Loc::Text("asset_browser.add_asset.material")))
				{
					QueueOperation({ EPendingOperationType::CreateMaterial, m_focusFolderPath, File::NULL_PATH });
				}
				if (ImGui::MenuItem(Loc::Text("asset_browser.add_asset.prefab")))
				{
					QueueOperation({ EPendingOperationType::CreatePrefab, m_focusFolderPath, File::NULL_PATH });
				}
				ImGui::EndMenu();
			}
			ImGui::Separator();
		}

		if (ImGui::MenuItem(Loc::Text("asset_browser.create_folder")))
		{
			QueueOperation({ EPendingOperationType::CreateFolder, m_focusFolderPath / File::Path("New Folder"), File::NULL_PATH });
		}
		if (ImGui::MenuItem(Loc::Text("common.refresh")))
		{
			m_folderChildrenCache.clear();
			m_entriesDirty = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem(Loc::Text("asset_browser.copy_folder_path")))
		{
			QueueOperation({ EPendingOperationType::CopyPath, m_focusFolderPath, File::NULL_PATH });
		}
		if (ImGui::MenuItem(Loc::Text("asset_browser.open_in_explorer")))
		{
			QueueOperation({ EPendingOperationType::OpenInExplorer, m_focusFolderPath, File::NULL_PATH });
		}
	}

	ImGui::EndPopup();
}

// ── DrawFolderTreeContextMenu ───────────────────────────────────────────────
// 폴더 트리 노드 우클릭 — 별도 팝업 ID.
// 트리는 항상 폴더만 다루므로 entry/빈공간 구분 없이 동일한 항목.
void CAssetBrowserTool::DrawFolderTreeContextMenu()
{
	constexpr const char* POPUP_ID = "##asset_browser_tree_popup";

	if (m_treeCtxOpenRequested)
	{
		ImGui::OpenPopup(POPUP_ID);
		m_treeCtxOpenRequested = false;
	}

	if (false == ImGui::BeginPopup(POPUP_ID))
	{
		return;
	}

	if (m_treeCtxFolderPath.empty())
	{
		ImGui::EndPopup();
		return;
	}

	const File::Path&   folder   = m_treeCtxFolderPath;
	const ECtxRootKind  rootKind = ClassifyRoot(folder, m_assetRootPath, m_scriptRootPath);
	const bool          isRootSelf = folder == m_assetRootPath || folder == m_scriptRootPath;

	if (ImGui::MenuItem(Loc::Text("asset_browser.tree.focus_here")))
	{
		SetFocusFolderPath(folder);
	}
	if (ImGui::MenuItem(Loc::Text("asset_browser.open_in_explorer")))
	{
		QueueOperation({ EPendingOperationType::OpenInExplorer, folder, File::NULL_PATH });
	}
	if (ImGui::MenuItem(Loc::Text("asset_browser.copy_folder_path")))
	{
		QueueOperation({ EPendingOperationType::CopyPath, folder, File::NULL_PATH });
	}
	ImGui::Separator();

	if (ECtxRootKind::Scripts == rootKind)
	{
		if (ImGui::MenuItem(Loc::Text("asset_browser.add_script")))
		{
			QueueOperation({ EPendingOperationType::CreateScript, folder, File::NULL_PATH });
		}
	}
	else if (ECtxRootKind::Assets == rootKind)
	{
		if (ImGui::BeginMenu(Loc::Text("asset_browser.add_asset")))
		{
			if (ImGui::MenuItem(Loc::Text("asset_browser.add_asset.scene")))
			{
				QueueOperation({ EPendingOperationType::CreateScene, folder, File::NULL_PATH });
			}
			if (ImGui::MenuItem(Loc::Text("asset_browser.add_asset.material")))
			{
				QueueOperation({ EPendingOperationType::CreateMaterial, folder, File::NULL_PATH });
			}
			if (ImGui::MenuItem(Loc::Text("asset_browser.add_asset.prefab")))
			{
				QueueOperation({ EPendingOperationType::CreatePrefab, folder, File::NULL_PATH });
			}
			ImGui::EndMenu();
		}
	}
	if (ImGui::MenuItem(Loc::Text("asset_browser.create_folder")))
	{
		QueueOperation({ EPendingOperationType::CreateFolder, folder / File::Path("New Folder"), File::NULL_PATH });
	}

	// 루트 자체에는 rename/delete 위험하므로 비루트 폴더에만 노출.
	if (false == isRootSelf)
	{
		ImGui::Separator();
		if (ImGui::MenuItem(Loc::Text("common.delete")))
		{
			m_deleteTargetPath = folder;
			m_requestDeletePopup = true;
		}
	}

	ImGui::EndPopup();
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

void CAssetBrowserTool::StartRenameForNewPath(const File::Path& path)
{
	if (path.empty()) return;
	m_isRenaming = true;
	m_renamingPath = path;
	m_renameBuffer = ToUtf8(path.stem());
	m_entriesDirty = true;
}

void CAssetBrowserTool::ShowNewScriptPopup(const File::Path& parentFolder)
{
	if (false == Editor::ImEditor.IsValid())
	{
		return;
	}

	// 팝업의 입력 상태는 람다와 별도 lifetime — shared_ptr 로 보관해 매 프레임
	// 같은 인스턴스에 쓰여지도록 한다.
	auto state = std::make_shared<NewScriptInput>();
	state->ParentFolder = parentFolder;

	ImPopupDesc desc;
	desc.Title    = Loc::Text("asset_browser.script_popup.title");
	desc.Id       = "asset_browser/new_script";
	desc.InitSize = ImVec2(480.0f, 360.0f);
	desc.Flags    = ImGuiWindowFlags_NoCollapse;

	// 캡처: SafePtr<this> 대용으로 raw this 사용 — AssetBrowserTool 의 lifetime
	// 은 에디터 전체에 걸쳐 보장된다. state 는 shared_ptr 로 by-value 캡처.
	CAssetBrowserTool* self = this;
	desc.OnRenderStayFunc = [state, self](IImPopupWindow& popup)
	{
		ImGui::TextUnformatted(Loc::Text("asset_browser.script_popup.class_name"));
		ImGui::SetNextItemWidth(-FLT_MIN);
		ValidatedStringInput(
			"##script_class_name", &state->ClassName,
			/*invalid*/ state->ClassName.empty());

		ImGui::Spacing();
		ImGui::TextUnformatted(Loc::Text("asset_browser.script_popup.properties"));

		// 각 행: 타입 Combo + 이름 InputText. List 위젯이 + / - 버튼과 외곽 박스 담당.
		ImGui::Utillity::StyleBuilder styleBuilder;
		styleBuilder.PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3.0f, 3.0f));
		styleBuilder.PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
		ImList<NewScriptProperty>(
			"##script_props", state->Properties,
			[](NewScriptProperty& p, int /*idx*/)
			{
				// Combo 와 InputText 를 가로로 배치 — 각각 절반 폭.
				const float fullW = ImGui::CalcItemWidth();
				const float halfW = fullW * 0.5f - 4.0f;

				const char* items[SCRIPT_PROP_TYPES.size()];
				for (size_t i = 0; i < SCRIPT_PROP_TYPES.size(); ++i)
				{
					items[i] = SCRIPT_PROP_TYPES[i].first;
				}

				ImGui::SetNextItemWidth(halfW);
				ImGui::Combo("##type", &p.TypeIndex, items,
					static_cast<int>(SCRIPT_PROP_TYPES.size()));
				ImGui::SameLine(0.0f, 4.0f);
				ImGui::SetNextItemWidth(halfW);
				ImInputText input;
				input.SetText(p.Name);
				input.SetHintText(Loc::Text("asset_browser.script_popup.property_hint"));
				input(ImGuiInputTextFlags_None, p.Name.empty());
				p.Name = input;
			},
			NewScriptProperty{});
		styleBuilder.PopStyle();

		ImGui::Spacing();
		ImGui::Separator();

		// ── 하단 버튼 ─────────────────────────────────────────────────────
		// 모든 프로퍼티 이름이 비어있지 않아야 생성 가능.
		bool allPropsValid = true;
		for (const NewScriptProperty& p : state->Properties)
		{
			if (p.Name.empty()) { allPropsValid = false; break; }
		}
		const bool canCreate = false == state->ClassName.empty() && allPropsValid;
		ImGui::BeginDisabled(false == canCreate);
		if (ImGui::Button(Loc::Text("common.create")))
		{
			File::Path hPath, cppPath;
			if (ResolveScriptPaths(state->ParentFolder, state->ClassName, hPath, cppPath))
			{
				const std::string headerFile = ToUtf8(hPath.filename());
				WriteTextFile(hPath,   MakeScriptHeader(state->ClassName, state->Properties));
				WriteTextFile(cppPath, MakeScriptSource(state->ClassName, headerFile));
				self->m_entriesDirty = true;
			}
			popup.Close();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button(Loc::Text("common.cancel")))
		{
			popup.Close();
		}
	};

	Editor::ImEditor->OpenPopup(desc);
}

void CAssetBrowserTool::ShowScriptCompileFailurePopup(std::string message)
{
	if (false == Editor::ImEditor.IsValid() || message.empty())
	{
		return;
	}

	auto state = std::make_shared<std::string>(std::move(message));

	ImPopupDesc desc;
	desc.Title    = Loc::Text("asset_browser.compile_fail.title");
	desc.Id       = "asset_browser/compile_fail";
	desc.InitSize = ImVec2(720.0f, 460.0f);
	desc.Flags    = ImGuiWindowFlags_NoCollapse;

	desc.OnRenderStayFunc = [state](IImPopupWindow& popup)
	{
		ImGui::TextUnformatted(Loc::Text("asset_browser.compile_fail.description"));
		ImGui::Spacing();

		// 메시지 본문 — InputTextMultiline + ReadOnly. ReadOnly 플래그가 있어도
		// 텍스트 선택/드래그/Ctrl+C 는 모두 동작한다.
		const ImVec2 region = ImGui::GetContentRegionAvail();
		const float  footerH = ImGui::GetFrameHeightWithSpacing();
		const ImVec2 logSize(region.x, region.y - footerH - 8.0f);

		ImGui::InputTextMultiline("##compile_fail_log",
			state->data(), state->size() + 1,
			logSize,
			ImGuiInputTextFlags_ReadOnly);

		// ── 하단 버튼 (복사 / 확인) — 우측 정렬 ──────────────────────────────
		constexpr float BTN_W = 90.0f;
		const float footerAvailW = ImGui::GetContentRegionAvail().x;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + footerAvailW - BTN_W * 2.0f - 8.0f);

		if (ImGui::Button(Loc::Text("common.copy"), ImVec2(BTN_W, 0.0f)))
		{
			ImGui::SetClipboardText(state->c_str());
		}
		ImGui::SameLine();
		if (ImGui::Button(Loc::Text("common.ok"), ImVec2(BTN_W, 0.0f)))
		{
			popup.Close();
		}
	};

	Editor::ImEditor->OpenPopup(desc);
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

	// 새 이름의 마지막 확장자가 원본과 정확히 같지 않으면 원본 확장자를 강제 부여한다.
	// 예: 원본이 NewScene.jscene 일 때
	//   "test.Jscene" → 그대로
	//   "test"        → "test.jscene"
	//   "test.dd"     → "test.dd.jscene"
	// 폴더 rename (원본 확장자 없음) 인 경우엔 변경 없이 그대로.
	std::wstring inputName = Utillity::U8ToWString(m_renameBuffer);
	const std::wstring originalExt = sourcePath.extension().wstring();
	if (false == originalExt.empty())
	{
		const std::filesystem::path inputAsPath(inputName);
		const std::wstring inputExt = inputAsPath.extension().wstring();
		// 대소문자 무시 비교.
		auto lower = [](std::wstring s)
		{
			std::transform(s.begin(), s.end(), s.begin(),
				[](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
			return s;
		};
		if (lower(inputExt) != lower(originalExt))
		{
			inputName += originalExt;
		}
	}

	File::Path targetPath = sourcePath.parent_path() / File::Path(inputName);
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
