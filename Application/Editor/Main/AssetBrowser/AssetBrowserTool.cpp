#include "pch.h"
#include "AssetBrowserTool.h"
#include "AssetHandler.h"

#include "Editor/Editor.h"
#include "Editor/Command/EditorFileCommands.h"
#include "Editor/EditorDragDrop.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/Core/Core.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Core/Renderer/IRenderResourceCache.h"
#include "Engine/GameFramework/Reflection/ReflectionRegistry.h" // 스크립트 타입 목록(팝업 Ref 옵션)
#include "Engine/Core/Asset/AssetPath.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/IAssetRegistry.h"
#include "Engine/Core/Asset/SpriteAsset.h"
#include "Engine/Core/Resource/ResourceRegistry.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/IRHITexture.h"
#include "Engine/Editor/ImGuiUtillity.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/ImItem/ImTree.h"
#include "Engine/Editor/ImItem/ImSplitter.h"
#include "Engine/Editor/ImItem/ImText.h"
#include "Engine/Editor/ImItem/ImList.h"
#include "Engine/Editor/ImWindow/ImWindowContext.h"
#include "Engine/Editor/ImWindow/IImPopupWindow.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "Engine/GameFramework/Rendering/SpriteRenderSystem.h"
#include "Utillity/File/FileUtillities.h"
#include "Utillity/String/StringUtillity.h"

#include <array>
#include <cctype>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_set>

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
		std::string TypeToken = "Float";       // 1차 콤보 선택. "Ref" 면 참조(RefTarget 사용).
		std::string RefTarget = "GameObject";  // TypeToken=="Ref" 일 때 참조 대상 타입.
		std::string Name      = "";
	};
	struct NewScriptInput
	{
		std::string                    ClassName    = "NewScript";
		std::vector<NewScriptProperty> Properties;
		File::Path                     ParentFolder;
	};

	// 1차 콤보: 기본 타입 토큰 (라벨=토큰, PascalCase). "Ref" 는 참조 — 2차 콤보로 대상 선택.
	// Int/UInt 는 64비트(나중에 32비트가 필요하면 Int32/UInt32 를 따로 명시).
	constexpr std::array<const char*, 11> SCRIPT_BASE_TYPES = {
		"Bool", "Int", "UInt", "Float", "Degree", "Radian",
		"String", "Vector2", "Rect", "Asset", "Ref",
	};

	// 2차 콤보(Ref 대상): GameObject + 등록된 컴포넌트 + 등록된 스크립트.
	std::vector<std::string> BuildRefTargets()
	{
		std::vector<std::string> out = { "GameObject" };
		if (Core::Reflection.IsValid())
		{
			for (std::size_t i = 0; i < Core::Reflection->GetComponentTypeCount(); ++i)
			{
				const ComponentTypeInfo* ti = Core::Reflection->GetComponentType(i);
				if (nullptr == ti || nullptr == ti->Type.Name) continue;
				const char* n = ti->Type.Name;
				if (0 == std::strcmp(n, "GameObject")) continue;            // 위에서 이미 추가
				if (0 == std::strcmp(n, "TransformHierarchy2D")) continue;  // 내부용
				if (0 == std::strcmp(n, "ScriptComponent")) continue;       // 컨테이너
				out.push_back(n);
			}
			for (std::size_t i = 0; i < Core::Reflection->GetScriptTypeCount(); ++i)
			{
				const ScriptTypeInfo* si = Core::Reflection->GetScriptType(i);
				if (nullptr == si || nullptr == si->Type.Name) continue;
				out.push_back(si->Type.Name);
			}
		}
		return out;
	}

	// 프로퍼티 → 생성 헤더에 쓸 최종 C++ 타입 토큰.
	std::string FinalTypeToken(const NewScriptProperty& p)
	{
		if (p.TypeToken == "Ref")
		{
			return "Ref<" + p.RefTarget + ">";
		}
		return p.TypeToken;
	}

	std::string DefaultValueForToken(const std::string& finalToken)
	{
		if (finalToken == "Bool")   return "false";
		if (finalToken == "Int")    return "0";
		if (finalToken == "UInt")   return "0u";
		if (finalToken == "Float")  return "0.0f";
		if (finalToken == "Degree") return "Degree(0.0f)";
		if (finalToken == "Radian") return "Radian(0.0f)";
		if (finalToken == "String") return "\"\"";
		return "{}";   // Vector2 / Rect / Asset / Ref<...>
	}

	// 유효한 C++ 식별자인지 — 첫 글자는 알파/밑줄, 이후 알파넘/밑줄.
	bool IsValidCppIdentifier(const std::string& s)
	{
		if (s.empty()) return false;
		const unsigned char first = static_cast<unsigned char>(s[0]);
		if (false == (std::isalpha(first) || s[0] == '_')) return false;
		for (char c : s)
		{
			const unsigned char ch = static_cast<unsigned char>(c);
			if (false == (std::isalnum(ch) || c == '_')) return false;
		}
		return true;
	}

	// 프로퍼티 이름이 스크립트에서 예약된(라이프사이클/베이스) 이름과 충돌하는지.
	bool IsReservedScriptName(const std::string& name)
	{
		static const std::array<const char*, 7> kReserved = {
			"OnCreate", "OnStart", "OnUpdate", "OnFixedUpdate", "OnDestroy", "GetOwner", "GetScene"
		};
		for (const char* r : kReserved)
		{
			if (name == r) return true;
		}
		return false;
	}

	// UI 검증: 프로퍼티 이름이 무효(빈 값/식별자 아님/예약어)이거나 목록 내 중복인지.
	bool IsPropNameInvalid(const std::string& name, const std::vector<NewScriptProperty>& all)
	{
		if (name.empty() || false == IsValidCppIdentifier(name) || IsReservedScriptName(name))
		{
			return true;
		}
		int count = 0;
		for (const NewScriptProperty& p : all)
		{
			if (p.Name == name) ++count;
		}
		return count > 1;
	}

	std::string MakeScriptHeader(const std::string& className,
	                             const std::vector<NewScriptProperty>& props)
	{
		std::ostringstream out;
		out << "#pragma once\n\n";
		out << "#include \"GameFramework/Scripting/ScriptAPI.h\"\n";
		// Ref<X> 는 대상 타입 X 의 헤더가 필요 — 대상별로 include 를 자동 추가한다.
		//   스크립트  → "Scripts/<X>.h"
		//   컴포넌트  → "GameFramework/Component/<X>.h" (대부분 개별 헤더. 그룹 헤더 타입은 직접 수정 필요)
		//   GameObject→ ScriptAPI 가 이미 include (추가 불필요)
		{
			std::unordered_set<std::string> includes;
			for (const NewScriptProperty& p : props)
			{
				if (p.TypeToken != "Ref" || p.RefTarget.empty() || p.RefTarget == className) continue;
				if (p.RefTarget == "GameObject") continue;
				const bool isScript = Core::Reflection.IsValid()
					&& nullptr != Core::Reflection->FindScriptByName(p.RefTarget.c_str());
				includes.insert(isScript
					? ("Scripts/" + p.RefTarget + ".h")
					: ("GameFramework/Component/" + p.RefTarget + ".h"));
			}
			for (const std::string& inc : includes)
			{
				out << "#include \"" << inc << "\"\n";
			}
		}
		out << "\n";
		out << "JBRO_SCRIPT " << className << " final : public CGameScript\n";
		out << "{\n";
		out << "public:\n";
		if (false == props.empty())
		{
			// JPROP 어트리뷰트 방식으로 자동 등록(헤더 스캐너가 파싱). 안전망: 어떤 입력이
			// 와도 유효한 C++ 가 나오도록 무효 식별자/예약어/중복 이름은 건너뛴다.
			std::unordered_set<std::string> usedNames;
			for (const NewScriptProperty& p : props)
			{
				if (false == IsValidCppIdentifier(p.Name) || IsReservedScriptName(p.Name)) continue;
				if (false == usedNames.insert(p.Name).second) continue;   // 중복 이름 스킵
				const std::string token = FinalTypeToken(p);
				out << "\tJPROP() " << token << " "
				    << p.Name << " = " << DefaultValueForToken(token) << ";\n";
			}
			out << "\n";
		}
		out << "protected:\n";
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
		case EAssetType::Sprite:
			return "[SPR]";
		case EAssetType::Material:
			return "[MAT]";
		case EAssetType::Scene:
			return "[SCN]";
		case EAssetType::Prefab:
			return "[PFB]";
		case EAssetType::Script:
			return "[SCR]";
		default:
			return "[FILE]";
		}
	}

	// entry 타입에 대응하는 ResourceRegistry 아이콘 키.
	// 이미지 확장자는 Thumbnail 에 실제 자산을 직접 로드하므로 여기서 처리하지 않는다.
	const char* GetIconResourceKey(const AssetBrowserEntry& entry)
	{
		if (entry.IsDirectory) return "icon-folder";
		switch (entry.Type)
		{
		case EAssetType::Scene:
			return "icon-scene";
		case EAssetType::Script:
			return "icon-script";
		case EAssetType::Material:
			return "icon-material";
		case EAssetType::Prefab:
			return "icon-object";
		case EAssetType::Audio:
			return "icon-audio";
		default:
			return "icon-file-default";
		}
	}

	bool IsImageExtension(const std::string& ext)
	{
		return ext == ".png" || ext == ".jpg" || ext == ".jpeg"
		    || ext == ".bmp" || ext == ".tga";
	}

	// SpriteAsset 의 GPU 텍스처 핸들 → ImTextureID. cache 에 없으면 lazy 생성.
	ImTextureID GetSpriteImTexture(CSpriteAsset* sprite)
	{
		if (nullptr == sprite) return 0;
		if (false == Engine.RenderResourceCache.IsValid()) return 0;
		SafePtr<IRHITexture> tex = Engine.RenderResourceCache->AcquireSpriteTexture(sprite->GetGuid(), *sprite);
		if (false == tex.IsValid()) return 0;
		void* srv = tex->GetNativeHandle().ShaderResourceView;
		return reinterpret_cast<ImTextureID>(srv);
	}

	// AssetBrowserEntry 의 두 가지 썸네일 소스(AssetRef 또는 ResourceRegistry raw) 중 라이브한 쪽 반환.
	CSpriteAsset* PickEntryThumbnail(const AssetBrowserEntry& entry)
	{
		if (entry.Thumbnail.IsValid()) return entry.Thumbnail.Get();
		return entry.ThumbnailRaw;
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
				AssetRef<IAsset> asset = assetManager->LoadAssetByPath(entry.AbsolutePath);
				if (asset.IsValid() && EAssetType::Sprite == asset->GetAssetType())
				{
					entry.Thumbnail = StaticAssetRefCast<CSpriteAsset>(asset);
				}
			}
		}

		if (false == entry.Thumbnail.IsValid() && nullptr == entry.ThumbnailRaw)
		{
			entry.ThumbnailRaw = Core::ResourceRegistry->GetSprite(GetIconResourceKey(entry));
		}

		// GPU 텍스처가 아직 없으면 lazy 생성 (cache 경유).
		CSpriteAsset* thumb = entry.Thumbnail.IsValid() ? entry.Thumbnail.Get() : entry.ThumbnailRaw;
		if (thumb && Engine.RenderResourceCache.IsValid())
		{
			Engine.RenderResourceCache->AcquireSpriteTexture(thumb->GetGuid(), *thumb);
		}
	}

	// 드래그 소스 시작. 드래그가 활성화되면 true 반환(호출부가 m_dragPrimaryPath 기록).
	bool BeginAssetDragDropSource(const AssetBrowserEntry& entry)
	{
		EditorDragDrop::AssetPayloadDesc desc;
		desc.Guid              = entry.Guid;
		desc.RelativePath      = entry.RelativePath;
		desc.Type              = entry.Type;
		desc.IsDirectory       = entry.IsDirectory;
		desc.PreviewLabel      = entry.DisplayNameUtf8.c_str();
		desc.PreviewTextureID  = GetSpriteImTexture(PickEntryThumbnail(entry));
		desc.PreviewSize       = 56.0f;
		return EditorDragDrop::BeginAssetDragDropSource(desc);
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
	m_selection.Clear();   // 폴더가 바뀌면 다중 선택 초기화
	CancelRename();
	m_entriesDirty = true;
}

void CAssetBrowserTool::ResetProjectState()
{
	m_assetRootPath = File::NULL_PATH;
	m_scriptRootPath = File::NULL_PATH;
	m_focusFolderPath = File::NULL_PATH;
	m_selectedEntryPath = File::NULL_PATH;
	m_selection.Clear();
	m_dragPrimaryPath = File::NULL_PATH;
	m_clipboardPaths.clear();
	m_clipboardIsCut = false;
	m_entries.clear();
	m_filteredEntryIndices.clear();
	m_folderChildrenCache.clear();
	m_backStack.clear();
	m_forwardStack.clear();
	m_pendingOperations.clear();
	m_searchText.clear();
	m_lastSearchText.clear();
	m_deleteTargets.clear();
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
		// 다중 선택 저장소 식별자 — 경로 해시로 프레임/리프레시 간 안정.
		browserEntry.SelectionId = ImHashStr(browserEntry.AbsolutePathUtf8.c_str());

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

	// 스크립트 트리(.cpp/.h)가 바뀌면 GameScript 프로젝트(.vcxproj 명시 목록)를 재생성해야 한다.
	bool scriptTreeChanged = false;

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
			// Asset / Script 루트 어디서든 폴더 생성 허용(IsInsideAnyRoot 는 위에서 보장).
			Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CCreateFolderCommand>(operation.Path), "__AssetDatabase");
			break;
		case EPendingOperationType::Rename:
		{
			// 같은 루트(Asset 또는 Script) 안에서의 이름변경만 허용.
			const bool sameRoot = (IsInsideAssetRoot(operation.Path)  && IsInsideAssetRoot(operation.TargetPath))
			                   || (IsInsideScriptRoot(operation.Path) && IsInsideScriptRoot(operation.TargetPath));
			if (sameRoot)
			{
				const File::Path& root = insideAssetRoot ? m_assetRootPath : m_scriptRootPath;
				Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CRenamePathCommand>(operation.Path, operation.TargetPath, root), "__AssetDatabase");
				if (false == insideAssetRoot) scriptTreeChanged = true;
			}
			break;
		}
		case EPendingOperationType::Delete:
		{
			// Asset / Script 루트 모두 삭제 허용(예전엔 Asset 루트만 가능했다).
			const File::Path& root = insideAssetRoot ? m_assetRootPath : m_scriptRootPath;
			Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CDeletePathCommand>(operation.Path, root), "__AssetDatabase");
			if (false == insideAssetRoot)
			{
				scriptTreeChanged = true;
				// 스크립트 소스는 .cpp/.h 한 쌍 — 짝 파일도 함께 삭제해 고아 헤더로 인한
				// 링크 에러를 막는다.
				const std::string ext = operation.Path.extension().generic_string();
				std::vector<const char*> siblingExts;
				if (ext == ".cpp" || ext == ".cc" || ext == ".cxx")      siblingExts = { ".h", ".hpp", ".hxx" };
				else if (ext == ".h" || ext == ".hpp" || ext == ".hxx")  siblingExts = { ".cpp", ".cc", ".cxx" };
				for (const char* se : siblingExts)
				{
					File::Path sibling = operation.Path;
					sibling.replace_extension(se);
					if (std::filesystem::exists(sibling, errorCode))
					{
						Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CDeletePathCommand>(sibling, root), "__AssetDatabase");
						break;
					}
					errorCode.clear();
				}
			}
			break;
		}
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

		case EPendingOperationType::MoveInto:
		{
			// operation.Path = 원본, operation.TargetPath = 대상 폴더.
			if (false == CanPlaceInto(operation.Path, operation.TargetPath, /*isMove*/ true)) break;
			const File::Path dst = operation.TargetPath / operation.Path.filename();
			// Asset root 내 이동은 커맨드(메타 동반 + 언두). Script root 내 이동은 단순 파일 이동.
			if (IsInsideAssetRoot(operation.Path))
			{
				Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CRenamePathCommand>(operation.Path, dst, m_assetRootPath), "__AssetDatabase");
			}
			else
			{
				std::filesystem::create_directories(dst.parent_path(), errorCode);
				errorCode.clear();
				if (false == std::filesystem::exists(dst, errorCode))
				{
					std::filesystem::rename(operation.Path, dst, errorCode);
				}
				errorCode.clear();
			}
			break;
		}

		case EPendingOperationType::CopyInto:
		{
			// operation.Path = 원본, operation.TargetPath = 대상 폴더. 폴더 자신/하위 금지.
			if (false == CanPlaceInto(operation.Path, operation.TargetPath, /*isMove*/ false)) break;
			if (std::filesystem::is_directory(operation.Path, errorCode))
			{
				// 폴더는 고유 이름으로 재귀 복사한 뒤, 복사본 안의 .Jmeta 를 제거한다.
				// (단일 Duplicate 와 동일한 이유 — 메타를 남기면 원본과 GUID 가 충돌하므로
				//  AssetWatcher 가 새 GUID 로 재임포트하도록 메타를 떼어낸다.)
				errorCode.clear();
				const File::Path dst = MakeUniqueFilePath(operation.TargetPath, ToUtf8(operation.Path.filename()), "");
				if (false == dst.empty())
				{
					std::filesystem::copy(operation.Path, dst,
						std::filesystem::copy_options::recursive, errorCode);
					errorCode.clear();
					for (const std::filesystem::directory_entry& sub :
					     std::filesystem::recursive_directory_iterator(dst, errorCode))
					{
						if (errorCode) { errorCode.clear(); continue; }
						if (CAssetPath::IsMetaPath(sub.path().generic_string().c_str()))
						{
							std::filesystem::remove(sub.path(), errorCode);
							errorCode.clear();
						}
					}
				}
				errorCode.clear();
				break;
			}
			errorCode.clear();
			if (false == std::filesystem::is_regular_file(operation.Path, errorCode))
			{
				errorCode.clear();
				break;
			}
			errorCode.clear();
			const std::string stem = ToUtf8(operation.Path.stem());
			const std::string ext  = operation.Path.has_extension() ? operation.Path.extension().generic_string() : std::string{};
			const File::Path dst = MakeUniqueFilePath(operation.TargetPath, stem, ext);
			if (false == dst.empty())
			{
				CopyFileOnce(operation.Path, dst);
			}
			break;
		}

		default:
			break;
		}
	}

	// 스크립트 파일이 추가/삭제/이동되었으면 GameScript 프로젝트를 재생성해 vcxproj
	// 소스 목록과 생성 레지스트리를 디스크 상태와 일치시킨다(빌드 정합성).
	if (scriptTreeChanged)
	{
		if (SafePtr<CProjectManager> pm = Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr)
		{
			pm->RegenerateScriptProject();
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

	// 좌측 패널 = "콘텐츠"(폴더 트리) + "즐겨찾기"(추후 구현) CollapsingHeader.
	// (두 키는 로컬라이제이션에 추가해 두었다 — 이전엔 키가 없어 깨졌던 것.)
	ImGui::BeginChild("AssetBrowserFolderTree", ImVec2(availSpace.x * splitRatio, 0.0f), true);
	if (ImGui::CollapsingHeader(Loc::Text("asset_browser.contents_folders"), ImGuiTreeNodeFlags_DefaultOpen))
	{
		DrawFolderTree();
	}
	if (ImGui::CollapsingHeader(Loc::Text("asset_browser.favorite_folders"), ImGuiTreeNodeFlags_DefaultOpen))
	{
		// 즐겨찾기 폴더 목록 — 추후 구현 예정.
	}
	ImGui::EndChild();

	// 픽셀 기반 스플리터 — 영역 원점과 현재 픽셀 위치를 계산해 비율로 환산.
	{
		const ImVec2 regionMin = ImGui::GetCursorScreenPos();
		float splitPos = availSpace.x * splitRatio;   // regionMin.x 기준 현재 픽셀 위치
		if (VerticalSplitter("##InspSplitter", splitPos, regionMin, availSpace, SPLITTER_W))
		{
			splitRatio = std::clamp(splitPos / std::max(availSpace.x, 1.0f), MIN_RATIO, MAX_RATIO);
		}
	}

	// NoMove 필수: 이게 없으면 빈 공간 좌클릭이 "윈도우 이동" ActiveId 를 선점해
	// EndMultiSelect 의 박스 선택 시작 조건(g.ActiveId == 0)이 깨져 드래그 박스 선택이
	// 전혀 시작되지 않는다. (ShowExampleAppAssetsBrowser 의 child 도 동일 플래그 사용.)
	ImGui::BeginChild("AssetBrowserEntries", ImVec2(0.0f, 0.0f),
	                  ImGuiChildFlags_Borders, ImGuiWindowFlags_NoMove);
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
	// 트리 노드도 드롭 타겟 — 이 폴더로 에셋 이동.
	{
		EditorDragDrop::AssetPayload payload;
		if (EditorDragDrop::AcceptAssetDragDropPayload(payload))
		{
			DropAssetsIntoFolder(folderPath);
		}
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
	// 다중 선택은 List/Icon 모두 ImGui Multi-Select API 로 처리한다.
	// 각 뷰가 BeginMultiSelect/EndMultiSelect 로 m_selection 을 갱신하고,
	// 사용자 입력으로 선택이 바뀐 프레임이면 여기서 Editor 선택을 동기화한다.
	m_selectionChangedThisFrame = false;

	if (m_viewMode == EViewMode::List)
	{
		DrawListEntries();
	}
	else
	{
		DrawIconEntries();
	}

	if (m_selectionChangedThisFrame)
	{
		SyncEditorSelection();
	}
}

void CAssetBrowserTool::DrawListEntries()
{
	const int itemCount = static_cast<int>(m_filteredEntryIndices.size());

	const ImGuiTableFlags flags =
		ImGuiTableFlags_BordersInnerV |
		ImGuiTableFlags_Resizable |
		ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_NoBordersInBody;
	// 폭은 0 → 패널을 가득 채움. (이전엔 700px 하드코딩으로 패널을 못 채우는 버그였음.)
	if (false == ImGui::BeginTable("AssetBrowserList", 4, flags, ImVec2(0.0f, 0.0f)))
	{
		return;
	}

	ImGui::TableSetupColumn(Loc::Text("common.name"), ImGuiTableColumnFlags_WidthFixed, 250.0f);
	ImGui::TableSetupColumn(Loc::Text("common.type"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
	ImGui::TableSetupColumn(Loc::Text("common.modified"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
	ImGui::TableSetupColumn(Loc::Text("common.guid"), ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupScrollFreeze(0, 1);   // 헤더 행 고정
	ImGui::TableHeadersRow();

	// 풀로우 선택이므로 BoxSelect1d. Esc/빈공간 클릭으로 선택 해제.
	// NoSelectOnRightClick: 우클릭 선택은 OpenBodyContextMenuForEntry 가 직접 관리
	// (다중 선택 위에서 우클릭 시 선택 유지 → 다중 삭제 가능).
	// (테이블은 자체 ScrollY 자식을 가지므로 BeginMultiSelect 를 테이블 안에서 호출해야
	//  박스 선택/빈공간 클릭 스코프가 스크롤 영역과 일치한다 — ImGui 표준 패턴.)
	// BoxSelect1d 는 "빈 공간에서 누른 경우"에만 켠다 — 아이템 행 위에서 누른 드래그는
	// 단일 에셋 드래그-드랍이어야 하기 때문(m_boxSelectFromVoid 참고, 아이콘 뷰와 동일 규칙).
	m_entryHoveredThisFrame = false;
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		m_boxSelectFromVoid = !m_entryHoveredPrevFrame;
	}
	// 빈 공간 클릭으로는 선택을 해제하지 않는다(ClearOnClickVoid 제외) — 인스펙터
	// 포커스가 사라지는 게 더 불편하다는 사용자 피드백. 선택 해제는 Esc 로만.
	ImGuiMultiSelectFlags msFlags =
		ImGuiMultiSelectFlags_ClearOnEscape |
		ImGuiMultiSelectFlags_NoSelectOnRightClick;
	if (m_boxSelectFromVoid)
	{
		msFlags |= ImGuiMultiSelectFlags_BoxSelect1d;
	}
	ImGuiMultiSelectIO* io = ImGui::BeginMultiSelect(msFlags, m_selection.Size, itemCount);
	m_selection.UserData = this;
	m_selection.AdapterIndexToStorageId = [](ImGuiSelectionBasicStorage* self, int idx) -> ImGuiID
	{
		CAssetBrowserTool* browser = static_cast<CAssetBrowserTool*>(self->UserData);
		return browser->m_entries[browser->m_filteredEntryIndices[static_cast<std::size_t>(idx)]].SelectionId;
	};
	ApplyMultiSelectRequests(io);

	{
		ImGuiListClipper clipper;
		clipper.Begin(itemCount);
		if (io->RangeSrcItem != -1)
		{
			clipper.IncludeItemByIndex(static_cast<int>(io->RangeSrcItem)); // Shift 범위 기준 행은 clip 제외
		}
		while (clipper.Step())
		{
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				AssetBrowserEntry& entry = m_entries[m_filteredEntryIndices[static_cast<std::size_t>(row)]];
				ImGui::PushID(entry.AbsolutePathUtf8.c_str()); // 캐시된 utf8 사용
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				const ImTextureID iconTex = GetSpriteImTexture(PickEntryThumbnail(entry));
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
					const bool selected = m_selection.Contains(entry.SelectionId);
					const std::string label = (0 != iconTex)
						? entry.DisplayNameUtf8
						: std::format("{} {}", GetEntryIcon(entry), entry.DisplayNameUtf8);
					ImGui::SetNextItemSelectionUserData(row);
					ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns);
					if (ImGui::IsItemHovered())
					{
						m_entryHoveredThisFrame = true; // 다음 프레임 박스 선택 가부 판정용
					}
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						OpenEntry(entry);
					}
					// 우클릭: 통합 컨텍스트 메뉴(SceneView 와 동일 패턴)
					if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
					{
						OpenBodyContextMenuForEntry(entry);
					}
					if (BeginAssetDragDropSource(entry))
					{
						m_dragPrimaryPath = entry.AbsolutePath;
					}
					// 폴더 행이면 드롭 타겟 — 이 폴더로 에셋 이동.
					if (entry.IsDirectory)
					{
						EditorDragDrop::AssetPayload payload;
						if (EditorDragDrop::AcceptAssetDragDropPayload(payload))
						{
							DropAssetsIntoFolder(entry.AbsolutePath);
						}
					}
				}

				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(entry.IsDirectory ? Loc::Text("common.folder") : GetTypeName(entry.Type));
				ImGui::TableSetColumnIndex(2);
				// 캐시된 ModifiedTimeText 사용 — 매 프레임 localtime/strftime 호출 제거
				ImGui::TextUnformatted(entry.IsDirectory ? "-" : entry.ModifiedTimeText.c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(entry.Guid.IsNull() ? "-" : ToUtf8(entry.Guid).c_str());
				ImGui::PopID();
			}
		}
	}

	io = ImGui::EndMultiSelect();
	ApplyMultiSelectRequests(io);

	m_entryHoveredPrevFrame = m_entryHoveredThisFrame;

	ImGui::EndTable();
}

void CAssetBrowserTool::DrawIconEntries()
{
	// ShowExampleAppAssetsBrowser() 패턴: 셀을 SetCursorScreenPos 로 명시 배치하고
	// 빈 Selectable + drawlist 오버레이로 그린다. BoxSelect2d 는 클리핑된 항목의
	// 가로 경계까지 갱신해야 하므로 명시적 좌표 배치가 정확도에 유리하다.
	const int itemCount   = static_cast<int>(m_filteredEntryIndices.size());
	const ImVec2 available = ImGui::GetContentRegionAvail();
	const int columnCount  = std::max(1, static_cast<int>(available.x / ICON_CELL_WIDTH));
	const int rowCount     = (itemCount + columnCount - 1) / columnCount;

	const float  cellW     = ICON_CELL_WIDTH  - 8.0f;
	const float  cellH     = ICON_CELL_HEIGHT - 8.0f;
	const float  imageSize = 56.0f;

	// 드래그(박스 선택, 2D) + Ctrl/Shift 다중 선택. Esc/빈공간 클릭으로 해제.
	// NoSelectOnRightClick: 우클릭 선택은 OpenBodyContextMenuForEntry 가 직접 관리.
	// BoxSelect2d 는 "빈 공간에서 누른 경우"에만 켠다 — 아이템 위에서 누른 드래그는
	// 단일 에셋 드래그-드랍이어야 하기 때문(아래 m_boxSelectFromVoid 참고).
	m_entryHoveredThisFrame = false;
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		m_boxSelectFromVoid = !m_entryHoveredPrevFrame;
	}
	// 빈 공간 클릭으로는 선택을 해제하지 않는다(ClearOnClickVoid 제외) — 인스펙터
	// 포커스가 사라지는 게 더 불편하다는 사용자 피드백. 선택 해제는 Esc 로만.
	ImGuiMultiSelectFlags msFlags =
		ImGuiMultiSelectFlags_ClearOnEscape |
		ImGuiMultiSelectFlags_NoSelectOnRightClick |
		ImGuiMultiSelectFlags_NavWrapX;
	if (m_boxSelectFromVoid)
	{
		msFlags |= ImGuiMultiSelectFlags_BoxSelect2d;
	}
	ImGuiMultiSelectIO* io = ImGui::BeginMultiSelect(msFlags, m_selection.Size, itemCount);
	m_selection.UserData = this;
	m_selection.AdapterIndexToStorageId = [](ImGuiSelectionBasicStorage* self, int idx) -> ImGuiID
	{
		CAssetBrowserTool* browser = static_cast<CAssetBrowserTool*>(self->UserData);
		return browser->m_entries[browser->m_filteredEntryIndices[static_cast<std::size_t>(idx)]].SelectionId;
	};
	ApplyMultiSelectRequests(io);

	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 startPos = ImGui::GetCursorScreenPos();

	ImGuiListClipper clipper;
	clipper.Begin(rowCount, ICON_CELL_HEIGHT);
	if (io->RangeSrcItem != -1)
	{
		clipper.IncludeItemByIndex(static_cast<int>(io->RangeSrcItem) / columnCount); // Shift 범위 기준 라인 clip 제외
	}
	while (clipper.Step())
	{
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
		{
			for (int column = 0; column < columnCount; ++column)
			{
				const int displayIndex = row * columnCount + column;
				if (displayIndex >= itemCount)
				{
					break;
				}

				AssetBrowserEntry& entry = m_entries[m_filteredEntryIndices[static_cast<std::size_t>(displayIndex)]];
				ImGui::PushID(entry.AbsolutePathUtf8.c_str()); // 캐시된 utf8 사용

				const ImVec2 cellPos(startPos.x + column * ICON_CELL_WIDTH,
				                     startPos.y + row    * ICON_CELL_HEIGHT);
				ImGui::SetCursorScreenPos(cellPos);

				const ImTextureID iconTex = GetSpriteImTexture(PickEntryThumbnail(entry));
				const bool isRenamingThis = m_isRenaming && entry.AbsolutePath == m_renamingPath;

				if (isRenamingThis)
				{
					// rename 중인 셀은 Multi-Select 대상에서 빼고(InputText 가 점유),
					// 아이콘 아래에 입력창만 띄운다.
					if (0 != iconTex)
					{
						const ImVec2 imgMin(cellPos.x + (cellW - imageSize) * 0.5f, cellPos.y + 4.0f);
						draw->AddImage(iconTex, imgMin, ImVec2(imgMin.x + imageSize, imgMin.y + imageSize));
					}
					ImGui::SetCursorScreenPos(ImVec2(cellPos.x, cellPos.y + 4.0f + imageSize + 2.0f));
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
					ImGui::PopID();
					continue;
				}

				// 셀 전체를 덮는 Selectable(다중 선택 단위) + 그 위에 아이콘/이름 오버레이.
				// 가시성은 Selectable 이 커서를 움직이기 전, cellPos 기준으로 캡처.
				const bool itemVisible = ImGui::IsRectVisible(ImVec2(cellW, cellH));
				bool selected = m_selection.Contains(entry.SelectionId);
				ImGui::SetNextItemSelectionUserData(displayIndex);
				ImGui::Selectable("##cell", selected, ImGuiSelectableFlags_None, ImVec2(cellW, cellH));
				if (ImGui::IsItemToggledSelection())
				{
					selected = !selected; // 색상 즉시 반영(EndMultiSelect 요청 기다리지 않음)
				}
				if (ImGui::IsItemHovered())
				{
					m_entryHoveredThisFrame = true; // 다음 프레임 박스 선택 가부 판정용
				}
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					OpenEntry(entry);
				}
				if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					OpenBodyContextMenuForEntry(entry);
				}
				if (BeginAssetDragDropSource(entry))
				{
					m_dragPrimaryPath = entry.AbsolutePath;
				}
				// 폴더 셀이면 드롭 타겟 — 이 폴더로 에셋 이동.
				if (entry.IsDirectory)
				{
					EditorDragDrop::AssetPayload payload;
					if (EditorDragDrop::AcceptAssetDragDropPayload(payload))
					{
						DropAssetsIntoFolder(entry.AbsolutePath);
					}
				}

				// BoxSelect2d 는 세로 클리핑이 헐거울 수 있어 렌더는 별도 가시성 검사.
				if (itemVisible)
				{
					const float padX = (cellW - imageSize) * 0.5f;
					if (0 != iconTex)
					{
						const ImVec2 imgMin(cellPos.x + padX, cellPos.y + 4.0f);
						draw->AddImage(iconTex, imgMin, ImVec2(imgMin.x + imageSize, imgMin.y + imageSize));
					}
					else
					{
						draw->AddText(ImVec2(cellPos.x + 4.0f, cellPos.y + 4.0f),
						              ImGui::GetColorU32(ImGuiCol_Text), GetEntryIcon(entry));
					}
					// 이름은 이미지 아래 가운데 정렬. 선택 시 강조색.
					const ImU32 textCol = ImGui::GetColorU32(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled);
					const ImVec2 textSize = ImGui::CalcTextSize(entry.DisplayNameUtf8.c_str(), nullptr, false, cellW);
					const ImVec2 textPos(cellPos.x + (cellW - std::min(textSize.x, cellW)) * 0.5f,
					                     cellPos.y + 4.0f + imageSize + 2.0f);
					draw->AddText(nullptr, 0.0f, textPos, textCol,
					              entry.DisplayNameUtf8.c_str(), nullptr, cellW);
				}

				ImGui::PopID();
			}
		}
	}
	clipper.End();

	io = ImGui::EndMultiSelect();
	ApplyMultiSelectRequests(io);

	m_entryHoveredPrevFrame = m_entryHoveredThisFrame;
}

// ── 컨텍스트 메뉴 열기 헬퍼 ─────────────────────────────────────────────────
// OpenPopup 은 호출 프레임 끝에 적용되며 같은 프레임의 IsMouseClicked 와
// 충돌하기 쉽다. 따라서 우클릭 감지 → 상태/플래그만 세팅, 실제 OpenPopup 은
// 각 DrawXxxContextMenu 의 첫머리에서 일괄 실행한다.
void CAssetBrowserTool::OpenBodyContextMenuForEntry(const AssetBrowserEntry& entry)
{
	// 이미 다중 선택에 포함된 항목을 우클릭하면 선택을 유지(다중 삭제 등).
	// 선택 밖 항목을 우클릭하면 그 항목만 단일 선택으로 교체.
	if (false == m_selection.Contains(entry.SelectionId))
	{
		SelectEntry(entry);
	}
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
			// 다중 선택 여부 — 단일 전용 동작(이름변경/복제)은 1개일 때만 노출.
			const int selectedCount = m_selection.Size;
			const bool multiSelected = selectedCount > 1;
			if (multiSelected)
			{
				ImGui::Text(Loc::Text("asset_browser.selection_count"), selectedCount);
				ImGui::Separator();
			}

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
			if (false == multiSelected && ImGui::MenuItem(Loc::Text("common.rename")))
			{
				StartRename(*targetEntry);
			}
			if (false == multiSelected && false == targetEntry->IsDirectory
			    && ImGui::MenuItem(Loc::Text("common.duplicate")))
			{
				QueueOperation({ EPendingOperationType::Duplicate, targetEntry->AbsolutePath, File::NULL_PATH });
			}

			// ── 잘라내기 / 복사 / 붙여넣기 ──────────────────────────────────
			ImGui::Separator();
			if (ImGui::MenuItem(Loc::Text("common.cut")))
			{
				CutToClipboard(CollectOperationTargets(targetEntry->AbsolutePath));
			}
			if (ImGui::MenuItem(Loc::Text("common.copy")))
			{
				CopyToClipboard(CollectOperationTargets(targetEntry->AbsolutePath));
			}
			// 폴더 우클릭이면 그 폴더로, 파일이면 현재 폴더로 붙여넣기.
			{
				const File::Path pasteFolder = targetEntry->IsDirectory ? targetEntry->AbsolutePath : m_focusFolderPath;
				ImGui::BeginDisabled(m_clipboardPaths.empty());
				if (ImGui::MenuItem(Loc::Text("common.paste")))
				{
					PasteIntoFolder(pasteFolder);
				}
				ImGui::EndDisabled();
			}

			ImGui::Separator();
			if (ImGui::MenuItem(Loc::Text("common.delete")))
			{
				// 우클릭 대상이 다중 선택에 포함돼 있으면 선택 전체를, 아니면 대상 1개를 삭제.
				m_deleteTargets = multiSelected ? CollectSelectedPaths()
				                                : std::vector<File::Path>{ targetEntry->AbsolutePath };
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
		ImGui::BeginDisabled(m_clipboardPaths.empty());
		if (ImGui::MenuItem(Loc::Text("common.paste")))
		{
			PasteIntoFolder(m_focusFolderPath);
		}
		ImGui::EndDisabled();
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
	ImGui::BeginDisabled(m_clipboardPaths.empty());
	if (ImGui::MenuItem(Loc::Text("common.paste")))
	{
		PasteIntoFolder(folder);
	}
	ImGui::EndDisabled();

	// 루트 자체에는 rename/delete 위험하므로 비루트 폴더에만 노출.
	if (false == isRootSelf)
	{
		ImGui::Separator();
		if (ImGui::MenuItem(Loc::Text("common.delete")))
		{
			m_deleteTargets = { folder };
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
		// 대상 목록을 모두 표시(다중 삭제 시 무엇을 지우는지 명확히).
		for (const File::Path& target : m_deleteTargets)
		{
			ImGui::BulletText("%s", ToUtf8(target).c_str());
		}
		ImGui::Separator();

		if (ImGui::Button(Loc::Text("common.delete")))
		{
			for (const File::Path& target : m_deleteTargets)
			{
				QueueOperation({ EPendingOperationType::Delete, target, File::NULL_PATH });
			}
			m_deleteTargets.clear();
			m_selection.Clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button(Loc::Text("common.cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			m_deleteTargets.clear();
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

void CAssetBrowserTool::SelectEntry(const AssetBrowserEntry& entry)
{
	// 단일 선택으로 교체.
	m_selection.Clear();
	m_selection.SetItemSelected(entry.SelectionId, true);
	m_selectedEntryPath = entry.AbsolutePath;
	Editor::ClearSelection();
	Editor::SelectAsset(entry.Guid, entry.AbsolutePath);
}

void CAssetBrowserTool::ApplyMultiSelectRequests(ImGuiMultiSelectIO* io)
{
	if (nullptr == io)
	{
		return;
	}
	// 요청이 있으면 사용자 입력으로 선택이 바뀐 것 → 이번 프레임 끝에 Editor 동기화.
	if (io->Requests.Size > 0)
	{
		m_selectionChangedThisFrame = true;
	}
	m_selection.ApplyRequests(io);
}

void CAssetBrowserTool::SyncEditorSelection()
{
	// 단일 선택 → 해당 에셋을 Inspector 대상으로. 다중/0 → 에셋 선택 해제.
	const AssetBrowserEntry* primary = nullptr;
	int count = 0;
	for (const AssetBrowserEntry& entry : m_entries)
	{
		if (m_selection.Contains(entry.SelectionId))
		{
			primary = &entry;
			if (++count > 1)
			{
				break;
			}
		}
	}

	Editor::ClearSelection();
	if (1 == count && primary)
	{
		m_selectedEntryPath = primary->AbsolutePath;
		Editor::SelectAsset(primary->Guid, primary->AbsolutePath);
	}
	else
	{
		m_selectedEntryPath = File::NULL_PATH;
		Editor::ClearAssetSelection();
	}
}

std::vector<File::Path> CAssetBrowserTool::CollectSelectedPaths() const
{
	std::vector<File::Path> paths;
	paths.reserve(static_cast<std::size_t>(m_selection.Size));
	for (const AssetBrowserEntry& entry : m_entries)
	{
		if (m_selection.Contains(entry.SelectionId))
		{
			paths.push_back(entry.AbsolutePath);
		}
	}
	return paths;
}

std::vector<File::Path> CAssetBrowserTool::CollectOperationTargets(const File::Path& contextEntryPath) const
{
	// 우클릭 대상이 다중 선택 안에 있으면 선택 전체, 아니면 그 항목만.
	std::vector<File::Path> selected = CollectSelectedPaths();
	const bool contextInSelection = std::any_of(selected.begin(), selected.end(),
		[&](const File::Path& p) { return p == contextEntryPath; });
	if (contextInSelection && selected.size() > 1)
	{
		return selected;
	}
	if (false == contextEntryPath.empty())
	{
		return { contextEntryPath };
	}
	return selected;
}

void CAssetBrowserTool::DropAssetsIntoFolder(const File::Path& targetFolder)
{
	if (targetFolder.empty())
	{
		return;
	}

	// 드래그된 항목이 선택에 포함돼 있으면 선택 전체를, 아니면 그 항목만 이동.
	const std::vector<File::Path> sources = CollectOperationTargets(m_dragPrimaryPath);
	for (const File::Path& source : sources)
	{
		if (CanPlaceInto(source, targetFolder, /*isMove*/ true))
		{
			QueueOperation({ EPendingOperationType::MoveInto, source, targetFolder });
		}
	}
	m_dragPrimaryPath = File::NULL_PATH;
}

void CAssetBrowserTool::CutToClipboard(std::vector<File::Path> paths)
{
	m_clipboardPaths = std::move(paths);
	m_clipboardIsCut = true;
}

void CAssetBrowserTool::CopyToClipboard(std::vector<File::Path> paths)
{
	m_clipboardPaths = std::move(paths);
	m_clipboardIsCut = false;
}

void CAssetBrowserTool::PasteIntoFolder(const File::Path& targetFolder)
{
	if (targetFolder.empty() || m_clipboardPaths.empty())
	{
		return;
	}

	const EPendingOperationType op = m_clipboardIsCut
		? EPendingOperationType::MoveInto
		: EPendingOperationType::CopyInto;
	for (const File::Path& source : m_clipboardPaths)
	{
		// 복사는 같은 폴더 사본을 허용해야 하므로 이동(cut)만 이 시점에 거른다.
		// (복사 유효성은 ProcessPendingOperations 의 CopyInto 에서 CanPlaceInto(isMove=false) 로 재검증.)
		if (m_clipboardIsCut && false == CanPlaceInto(source, targetFolder, /*isMove*/ true))
		{
			continue;
		}
		QueueOperation({ op, source, targetFolder });
	}

	// 잘라내기는 1회성 — 붙여넣은 뒤 비운다. 복사는 반복 붙여넣기 가능하도록 유지.
	if (m_clipboardIsCut)
	{
		m_clipboardPaths.clear();
		m_clipboardIsCut = false;
	}
}

bool CAssetBrowserTool::CanPlaceInto(const File::Path& source, const File::Path& targetFolder, bool isMove) const
{
	if (source.empty() || targetFolder.empty())
	{
		return false;
	}
	// 같은 루트 안에서만 허용(Asset↔Asset, Script↔Script).
	const bool sameAssetRoot  = IsInsideAssetRoot(source)  && IsInsideAssetRoot(targetFolder);
	const bool sameScriptRoot = IsInsideScriptRoot(source) && IsInsideScriptRoot(targetFolder);
	if (false == sameAssetRoot && false == sameScriptRoot)
	{
		return false;
	}
	// 이동은 이미 그 폴더에 있으면 금지(복사는 같은 폴더 사본을 허용).
	if (isMove && source.parent_path() == targetFolder)
	{
		return false;
	}
	// 폴더를 자기 자신 또는 그 하위로 배치 금지.
	const std::filesystem::path relative = std::filesystem::path(targetFolder).lexically_relative(source);
	if (false == relative.empty())
	{
		bool insideSource = (relative == std::filesystem::path("."));
		if (false == insideSource)
		{
			insideSource = true;
			for (const std::filesystem::path& part : relative)
			{
				if (part == "..") { insideSource = false; break; }
			}
		}
		if (insideSource)
		{
			return false;
		}
	}
	return true;
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
		const bool classNameInvalid = false == IsValidCppIdentifier(state->ClassName);
		ValidatedStringInput(
			"##script_class_name", &state->ClassName,
			/*invalid*/ classNameInvalid);

		ImGui::Spacing();
		ImGui::TextUnformatted(Loc::Text("asset_browser.script_popup.properties"));

		// 각 행: 타입 Combo + 이름 InputText. List 위젯이 + / - 버튼과 외곽 박스 담당.
		ImGui::Utillity::StyleBuilder styleBuilder;
		styleBuilder.PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3.0f, 3.0f));
		styleBuilder.PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
		ImList<NewScriptProperty>(
			"##script_props", state->Properties,
			[state](NewScriptProperty& p, int /*idx*/)
			{
				// 가로 배치: [타입 콤보][(Ref면) 대상 콤보][이름 인풋].
				// Ref 면 3열, 아니면 2열로 폭을 나눈다.
				const bool  isRef = (p.TypeToken == "Ref");
				const float fullW = ImGui::CalcItemWidth();
				const float cols  = isRef ? 3.0f : 2.0f;
				const float colW  = fullW / cols - 4.0f;

				// 1차 콤보: 기본 타입.
				int typeCur = 0;
				for (int i = 0; i < static_cast<int>(SCRIPT_BASE_TYPES.size()); ++i)
				{
					if (p.TypeToken == SCRIPT_BASE_TYPES[i]) { typeCur = i; break; }
				}
				ImGui::SetNextItemWidth(colW);
				if (ImGui::Combo("##type", &typeCur, SCRIPT_BASE_TYPES.data(),
					static_cast<int>(SCRIPT_BASE_TYPES.size())))
				{
					p.TypeToken = SCRIPT_BASE_TYPES[typeCur];
				}

				// 2차 콤보: Ref 대상(오브젝트/컴포넌트/스크립트).
				if (p.TypeToken == "Ref")
				{
					const std::vector<std::string> targets = BuildRefTargets();
					std::vector<const char*> tLabels;
					tLabels.reserve(targets.size());
					int tCur = 0;
					for (int i = 0; i < static_cast<int>(targets.size()); ++i)
					{
						tLabels.push_back(targets[i].c_str());
						if (targets[i] == p.RefTarget) tCur = i;
					}
					ImGui::SameLine(0.0f, 4.0f);
					ImGui::SetNextItemWidth(colW);
					if (ImGui::Combo("##reftarget", &tCur, tLabels.data(), static_cast<int>(tLabels.size())))
					{
						p.RefTarget = targets[tCur];
					}
				}

				ImGui::SameLine(0.0f, 4.0f);
				ImGui::SetNextItemWidth(colW);
				ImInputText input;
				input.SetText(p.Name);
				input.SetHintText(Loc::Text("asset_browser.script_popup.property_hint"));
				// 무효(빈 값/식별자 아님/예약어/중복) 이름은 빨간 프레임으로 표시.
				input(ImGuiInputTextFlags_None, IsPropNameInvalid(p.Name, state->Properties));
				p.Name = input;
			},
			NewScriptProperty{});
		styleBuilder.PopStyle();

		// 무효 사유 안내(중복/식별자) — 사용자가 왜 생성이 막히는지 알 수 있게.
		bool allPropsValid = true;
		for (const NewScriptProperty& p : state->Properties)
		{
			if (IsPropNameInvalid(p.Name, state->Properties)) { allPropsValid = false; break; }
		}
		if (false == allPropsValid)
		{
			ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%s", Loc::Text("asset_browser.script_popup.invalid_props"));
		}

		ImGui::Spacing();
		ImGui::Separator();

		// ── 하단 버튼 ─────────────────────────────────────────────────────
		// 클래스명이 유효 식별자이고, 모든 프로퍼티 이름이 유효+유일해야 생성 가능.
		const bool canCreate = false == classNameInvalid && allPropsValid;
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
				// 새 스크립트가 빌드에 즉시 포함되도록 GameScript 프로젝트를 재생성.
				if (SafePtr<CProjectManager> pm = Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr)
				{
					pm->RegenerateScriptProject();
				}
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
