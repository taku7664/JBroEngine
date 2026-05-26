#include "pch.h"
#include "ProjectManager.h"

#include "Core/Asset/AssetPath.h"
#include "Core/Asset/IAssetManager.h"
#include "Core/EngineContext.h"
#include "Core/Game/GameModuleTypes.h"
#include "Editor/ScriptModule/ScriptModuleLoader.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "yaml-cpp/yaml.h"

namespace
{
	constexpr const char* PROJECT_EXTENSION             = ".Jproject";
	constexpr const char* PROJECT_KEY_VERSION           = "Version";
	constexpr const char* PROJECT_KEY_ROOT_PATH         = "RootPath";
	constexpr const char* PROJECT_KEY_RES_WIDTH         = "ResolutionWidth";
	constexpr const char* PROJECT_KEY_RES_HEIGHT        = "ResolutionHeight";
	constexpr const char* PROJECT_KEY_SCENE_CAM_X       = "SceneViewCamX";
	constexpr const char* PROJECT_KEY_SCENE_CAM_Y       = "SceneViewCamY";
	constexpr const char* PROJECT_KEY_SCENE_CAM_SIZE    = "SceneViewCamSize";
	constexpr const char* PROJECT_KEY_SCRIPT_DLL_PATH   = "ScriptDllPath";
	constexpr const char* PROJECT_KEY_LAST_SCENE_PATH   = "LastOpenedScenePath";
	constexpr const char* PROJECT_KEY_PIXELS_PER_UNIT   = "PixelsPerUnit";
	constexpr const char* ASSETS_DIRECTORY_NAME         = "Assets";
}

bool CProjectManager::Initialize(const EngineContext& context)
{
	m_assetManager  = context.AssetManager;
	m_assetWatcher  = MakeOwnerPtr<CWindowsFileWatcher>();
	m_scriptLoader  = MakeOwnerPtr<CScriptModuleLoader>();
	m_engineContext = &context;
	m_info.OriginPath = File::Path(std::filesystem::current_path());
	m_isInitialized = m_assetManager.IsValid() && static_cast<bool>(m_assetWatcher);
	return m_isInitialized;
}

void CProjectManager::Finalize()
{
	CloseProject();
	m_scriptLoader.Reset();
	m_assetWatcher.Reset();
	m_assetManager  = nullptr;
	m_engineContext = nullptr;
	m_isInitialized = false;
}

bool CProjectManager::LoadProject(const ProjectLoadDesc& desc)
{
	if (false == m_isInitialized || false == m_assetManager.IsValid() || desc.ProjectFilePath.empty())
	{
		return false;
	}

	const std::filesystem::path projectPath = std::filesystem::absolute(desc.ProjectFilePath);
	if (projectPath.extension() != PROJECT_EXTENSION)
	{
		return false;
	}

	std::error_code errorCode;
	if (false == std::filesystem::exists(projectPath, errorCode) || false == std::filesystem::is_regular_file(projectPath, errorCode))
	{
		return false;
	}

	YAML::Node root;
	try
	{
		root = YAML::LoadFile(projectPath.string());
	}
	catch (const YAML::Exception&)
	{
		return false;
	}

	std::uint32_t version = 1;
	if (root[PROJECT_KEY_VERSION])
	{
		version = root[PROJECT_KEY_VERSION].as<std::uint32_t>(1);
	}

	std::uint32_t resolutionWidth  = 1920;
	std::uint32_t resolutionHeight = 1080;
	if (root[PROJECT_KEY_RES_WIDTH])
	{
		resolutionWidth = root[PROJECT_KEY_RES_WIDTH].as<std::uint32_t>(1920);
	}
	if (root[PROJECT_KEY_RES_HEIGHT])
	{
		resolutionHeight = root[PROJECT_KEY_RES_HEIGHT].as<std::uint32_t>(1080);
	}

	float sceneViewCamX    = 0.0f;
	float sceneViewCamY    = 0.0f;
	float sceneViewCamSize = 5.0f;
	if (root[PROJECT_KEY_SCENE_CAM_X])
	{
		sceneViewCamX = root[PROJECT_KEY_SCENE_CAM_X].as<float>(0.0f);
	}
	if (root[PROJECT_KEY_SCENE_CAM_Y])
	{
		sceneViewCamY = root[PROJECT_KEY_SCENE_CAM_Y].as<float>(0.0f);
	}
	if (root[PROJECT_KEY_SCENE_CAM_SIZE])
	{
		sceneViewCamSize = root[PROJECT_KEY_SCENE_CAM_SIZE].as<float>(5.0f);
		if (sceneViewCamSize <= 0.0f) sceneViewCamSize = 5.0f;
	}

	std::string scriptDllPath;
	if (root[PROJECT_KEY_SCRIPT_DLL_PATH])
	{
		scriptDllPath = root[PROJECT_KEY_SCRIPT_DLL_PATH].as<std::string>("");
	}

	std::string lastOpenedScenePath;
	if (root[PROJECT_KEY_LAST_SCENE_PATH])
	{
		lastOpenedScenePath = root[PROJECT_KEY_LAST_SCENE_PATH].as<std::string>("");
	}

	float pixelsPerUnit = 100.0f;
	if (root[PROJECT_KEY_PIXELS_PER_UNIT])
	{
		pixelsPerUnit = root[PROJECT_KEY_PIXELS_PER_UNIT].as<float>(100.0f);
		if (pixelsPerUnit < 1.0f) pixelsPerUnit = 1.0f;
	}

	std::filesystem::path rootRelativePath = ".";
	if (root[PROJECT_KEY_ROOT_PATH])
	{
		rootRelativePath = root[PROJECT_KEY_ROOT_PATH].as<std::string>();
	}
	if (rootRelativePath.is_absolute())
	{
		return false;
	}

	std::filesystem::path projectRootPath = std::filesystem::weakly_canonical(projectPath.parent_path() / rootRelativePath, errorCode);
	if (errorCode)
	{
		errorCode.clear();
		projectRootPath = std::filesystem::absolute(projectPath.parent_path() / rootRelativePath);
	}

	std::filesystem::path assetPath = projectRootPath / ASSETS_DIRECTORY_NAME;
	if (std::filesystem::exists(assetPath, errorCode) && false == std::filesystem::is_directory(assetPath, errorCode))
	{
		return false;
	}

	std::filesystem::create_directories(assetPath, errorCode);
	if (errorCode)
	{
		return false;
	}

	CloseProject();

	m_info.Version          = version;
	m_info.ProjectFilePath  = File::Path(projectPath);
	m_info.RootPath         = File::Path(projectRootPath);
	m_info.AssetPath        = File::Path(assetPath);
	m_info.ResolutionWidth  = (resolutionWidth  > 0) ? resolutionWidth  : 1920;
	m_info.ResolutionHeight = (resolutionHeight > 0) ? resolutionHeight : 1080;
	m_info.SceneViewCamX    = sceneViewCamX;
	m_info.SceneViewCamY    = sceneViewCamY;
	m_info.SceneViewCamSize = sceneViewCamSize;
	m_info.ScriptDllPath       = scriptDllPath;
	m_info.LastOpenedScenePath = lastOpenedScenePath;
	m_info.PixelsPerUnit       = pixelsPerUnit;

	if (false == m_assetManager->SetAssetRootPath(m_info.AssetPath.generic_string().c_str()))
	{
		return false;
	}

	FileWatcherDesc watcherDesc;
	watcherDesc.RootPath = m_info.AssetPath;
	watcherDesc.Recursive = true;
	m_assetWatcher->Watch(watcherDesc);

	for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(m_info.AssetPath, errorCode))
	{
		if (errorCode)
		{
			break;
		}
		if (entry.is_regular_file(errorCode))
		{
			ImportOrReloadAsset(File::Path(entry.path()));
		}
	}

	m_isProjectLoaded = true;
	MarkAssetDatabaseChanged();
	return true;
}

void CProjectManager::CloseProject()
{
	// 스크립트 DLL 먼저 언로드 (씬보다 먼저)
	if (m_scriptLoader)
	{
		m_scriptLoader->Unload();
	}

	if (m_assetWatcher)
	{
		m_assetWatcher->Stop();
	}
	m_isProjectLoaded = false;
	m_info.ProjectFilePath = File::NULL_PATH;
	m_info.RootPath        = File::NULL_PATH;
	m_info.AssetPath       = File::NULL_PATH;
	m_info.ScriptDllPath       = {};
	m_info.LastOpenedScenePath = {};
	MarkAssetDatabaseChanged();
}

void CProjectManager::Tick()
{
	if (false == m_isProjectLoaded || false == static_cast<bool>(m_assetWatcher))
	{
		return;
	}

	m_assetWatcher->Poll();
	std::vector<FileWatchEvent> events;
	if (m_assetWatcher->TakeEvents(events))
	{
		ProcessAssetEvents(events);
	}
}

bool CProjectManager::IsProjectLoaded() const
{
	return m_isProjectLoaded;
}

const File::Path& CProjectManager::GetOriginPath() const
{
	return m_info.OriginPath;
}

const File::Path& CProjectManager::GetProjectFilePath() const
{
	return m_info.ProjectFilePath;
}

const File::Path& CProjectManager::GetRootPath() const
{
	return m_info.RootPath;
}

const File::Path& CProjectManager::GetAssetPath() const
{
	return m_info.AssetPath;
}

std::uint64_t CProjectManager::GetAssetDatabaseRevision() const
{
	return m_assetDatabaseRevision;
}

SafePtr<IAssetManager> CProjectManager::GetAssetManager() const
{
	return m_assetManager;
}

void CProjectManager::BuildAssetRegistrySnapshot(AssetRegistrySnapshot& outSnapshot) const
{
	outSnapshot.Assets.clear();
	if (m_assetManager)
	{
		m_assetManager->GetRegistry().BuildSnapshot(outSnapshot);
	}
}

void CProjectManager::ProcessAssetEvents(const std::vector<FileWatchEvent>& events)
{
	for (const FileWatchEvent& event : events)
	{
		if (EFileWatchEventType::Created == event.Type)
		{
			TrySyncRenamedAssetMeta(event.Path, events);
		}
	}

	for (const FileWatchEvent& event : events)
	{
		ProcessAssetEvent(event);
	}
}

void CProjectManager::ProcessAssetEvent(const FileWatchEvent& event)
{
	if (false == m_assetManager.IsValid())
	{
		return;
	}

	if (EFileWatchEventType::Deleted == event.Type)
	{
		m_assetManager->RefreshAssetRegistry();
		MarkAssetDatabaseChanged();
		return;
	}

	if (ImportOrReloadAsset(event.Path))
	{
		MarkAssetDatabaseChanged();
	}
}

bool CProjectManager::ImportOrReloadAsset(const File::Path& absolutePath)
{
	if (false == IsImportableAssetPath(absolutePath))
	{
		return false;
	}

	std::string relativePath;
	if (false == MakeAssetRelativePath(absolutePath, relativePath))
	{
		return false;
	}

	const EAssetType type = DetectAssetType(relativePath);
	if (EAssetType::Unknown == type)
	{
		return false;
	}

	AssetImportDesc importDesc;
	importDesc.Type = type;
	importDesc.Path = relativePath.c_str();
	importDesc.Importer = "Default";
	AssetMetaData metaData;
	if (m_assetManager->ImportAsset(importDesc, &metaData))
	{
		m_assetManager->ReloadAsset(metaData.Guid);
		return true;
	}
	return false;
}

bool CProjectManager::IsImportableAssetPath(const File::Path& absolutePath) const
{
	if (absolutePath.empty() || CAssetPath::IsMetaPath(absolutePath.generic_string().c_str()))
	{
		return false;
	}

	std::error_code errorCode;
	return std::filesystem::exists(absolutePath, errorCode) && std::filesystem::is_regular_file(absolutePath, errorCode);
}

bool CProjectManager::MakeAssetRelativePath(const File::Path& absolutePath, std::string& outRelativePath) const
{
	std::error_code errorCode;
	std::filesystem::path relativePath = std::filesystem::relative(absolutePath, m_info.AssetPath, errorCode);
	if (errorCode)
	{
		return false;
	}

	return CAssetPath::NormalizeRelativePath(relativePath.generic_string().c_str(), outRelativePath);
}

EAssetType CProjectManager::DetectAssetType(const File::Path& relativePath) const
{
	std::string extension = relativePath.extension().generic_string();
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});

	if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tga")
	{
		return EAssetType::Texture;
	}
	if (extension == ".jscene")
	{
		return EAssetType::Scene;
	}
	if (extension == ".jprefab")
	{
		return EAssetType::Prefab;
	}
	if (extension == ".hlsl" || extension == ".wgsl" || extension == ".glsl")
	{
		return EAssetType::Shader;
	}
	if (extension == ".cpp" || extension == ".h" || extension == ".hpp")
	{
		return EAssetType::Script;
	}

	return EAssetType::Custom;
}

bool CProjectManager::TrySyncRenamedAssetMeta(const File::Path& createdAssetPath, const std::vector<FileWatchEvent>& events)
{
	if (createdAssetPath.empty() || CAssetPath::IsMetaPath(createdAssetPath.generic_string().c_str()))
	{
		return false;
	}

	std::error_code errorCode;
	if (false == std::filesystem::exists(createdAssetPath, errorCode) || false == std::filesystem::is_regular_file(createdAssetPath, errorCode))
	{
		return false;
	}

	File::Path newMetaPath;
	if (false == TryGetMetaPathForAsset(createdAssetPath, newMetaPath) || std::filesystem::exists(newMetaPath, errorCode))
	{
		return false;
	}
	errorCode.clear();

	const std::string newExtension = createdAssetPath.extension().generic_string();
	for (const FileWatchEvent& event : events)
	{
		if (EFileWatchEventType::Deleted != event.Type || event.Path.empty() || CAssetPath::IsMetaPath(event.Path.generic_string().c_str()))
		{
			continue;
		}

		if (event.Path.extension().generic_string() != newExtension)
		{
			continue;
		}

		if (MoveMetaForRenamedAsset(event.Path, createdAssetPath))
		{
			return true;
		}
	}

	return false;
}

bool CProjectManager::TryGetMetaPathForAsset(const File::Path& assetPath, File::Path& outMetaPath) const
{
	std::string relativePath;
	if (false == MakeAssetRelativePath(assetPath, relativePath))
	{
		return false;
	}

	outMetaPath = m_info.AssetPath / File::Path(CAssetPath::MakeMetaPath(relativePath.c_str()));
	return true;
}

bool CProjectManager::MoveMetaForRenamedAsset(const File::Path& oldAssetPath, const File::Path& newAssetPath) const
{
	File::Path oldMetaPath;
	File::Path newMetaPath;
	if (false == TryGetMetaPathForAsset(oldAssetPath, oldMetaPath) || false == TryGetMetaPathForAsset(newAssetPath, newMetaPath))
	{
		return false;
	}

	std::error_code errorCode;
	if (false == std::filesystem::exists(oldMetaPath, errorCode) || std::filesystem::exists(newMetaPath, errorCode))
	{
		return false;
	}
	errorCode.clear();

	if (newMetaPath.has_parent_path())
	{
		std::filesystem::create_directories(newMetaPath.parent_path(), errorCode);
		errorCode.clear();
	}

	std::filesystem::rename(oldMetaPath, newMetaPath, errorCode);
	return false == static_cast<bool>(errorCode);
}

void CProjectManager::MarkAssetDatabaseChanged()
{
	++m_assetDatabaseRevision;
}

std::uint32_t CProjectManager::GetResolutionWidth() const
{
	return m_info.ResolutionWidth;
}

std::uint32_t CProjectManager::GetResolutionHeight() const
{
	return m_info.ResolutionHeight;
}

void CProjectManager::SetResolution(std::uint32_t width, std::uint32_t height)
{
	m_info.ResolutionWidth  = (width  > 0) ? width  : 1920;
	m_info.ResolutionHeight = (height > 0) ? height : 1080;
}

float CProjectManager::GetPixelsPerUnit() const
{
	return m_info.PixelsPerUnit;
}

void CProjectManager::SetPixelsPerUnit(float ppu)
{
	m_info.PixelsPerUnit = (ppu >= 1.0f) ? ppu : 100.0f;
}

float CProjectManager::GetSceneViewCamX() const
{
	return m_info.SceneViewCamX;
}

float CProjectManager::GetSceneViewCamY() const
{
	return m_info.SceneViewCamY;
}

float CProjectManager::GetSceneViewCamSize() const
{
	return m_info.SceneViewCamSize;
}

void CProjectManager::SetSceneViewCamera(float x, float y, float size)
{
	m_info.SceneViewCamX    = x;
	m_info.SceneViewCamY    = y;
	m_info.SceneViewCamSize = (size > 0.0f) ? size : 5.0f;
}

const std::string& CProjectManager::GetScriptDllPath() const
{
	return m_info.ScriptDllPath;
}

void CProjectManager::SetScriptDllPath(const std::string& path)
{
	m_info.ScriptDllPath = path;
}

const std::string& CProjectManager::GetLastOpenedScenePath() const
{
	return m_info.LastOpenedScenePath;
}

void CProjectManager::SetLastOpenedScenePath(const std::string& relativePath)
{
	m_info.LastOpenedScenePath = relativePath;
}

bool CProjectManager::LoadScriptModule()
{
	if (false == m_isProjectLoaded || !m_scriptLoader || m_info.ScriptDllPath.empty())
	{
		return false;
	}

	// 이미 로드된 경우 먼저 언로드
	if (m_scriptLoader->IsLoaded())
	{
		m_scriptLoader->Unload();
	}

	// DLL 경로: 절대경로이면 그대로, 상대경로이면 프로젝트 루트 기준으로 결합
	std::filesystem::path dllPath(m_info.ScriptDllPath);
	if (false == dllPath.is_absolute() && false == m_info.RootPath.empty())
	{
		dllPath = m_info.RootPath / dllPath;
	}

	// GameModuleContext 구성
	GameModuleContext context;
	if (m_engineContext)
	{
		context.Platform          = m_engineContext->Platform.TryGet();
		context.MainRenderSurface = m_engineContext->MainRenderSurface.TryGet();
		context.RHIDevice         = m_engineContext->RHIDevice.TryGet();
		context.AssetManager      = m_assetManager.TryGet();
		context.Reflection        = m_engineContext->Reflection.TryGet();
	}

	return m_scriptLoader->Load(dllPath.string().c_str(), context);
}

void CProjectManager::UnloadScriptModule()
{
	if (m_scriptLoader)
	{
		m_scriptLoader->Unload();
	}
}

bool CProjectManager::IsScriptModuleLoaded() const
{
	return static_cast<bool>(m_scriptLoader) && m_scriptLoader->IsLoaded();
}

bool CProjectManager::SaveProject() const
{
	if (false == m_isProjectLoaded || m_info.ProjectFilePath.empty())
	{
		return false;
	}

	YAML::Emitter out;
	out << YAML::BeginMap;
	out << YAML::Key << PROJECT_KEY_VERSION         << YAML::Value << m_info.Version;
	out << YAML::Key << PROJECT_KEY_ROOT_PATH       << YAML::Value << ".";
	out << YAML::Key << PROJECT_KEY_RES_WIDTH       << YAML::Value << m_info.ResolutionWidth;
	out << YAML::Key << PROJECT_KEY_RES_HEIGHT      << YAML::Value << m_info.ResolutionHeight;
	out << YAML::Key << PROJECT_KEY_SCENE_CAM_X     << YAML::Value << m_info.SceneViewCamX;
	out << YAML::Key << PROJECT_KEY_SCENE_CAM_Y     << YAML::Value << m_info.SceneViewCamY;
	out << YAML::Key << PROJECT_KEY_SCENE_CAM_SIZE  << YAML::Value << m_info.SceneViewCamSize;
	out << YAML::Key << PROJECT_KEY_PIXELS_PER_UNIT << YAML::Value << m_info.PixelsPerUnit;
	if (false == m_info.ScriptDllPath.empty())
	{
		out << YAML::Key << PROJECT_KEY_SCRIPT_DLL_PATH << YAML::Value << m_info.ScriptDllPath;
	}
	if (false == m_info.LastOpenedScenePath.empty())
	{
		out << YAML::Key << PROJECT_KEY_LAST_SCENE_PATH << YAML::Value << m_info.LastOpenedScenePath;
	}
	out << YAML::EndMap;

	std::ofstream file(m_info.ProjectFilePath, std::ios::out | std::ios::trunc);
	if (false == file.is_open())
	{
		return false;
	}
	file << out.c_str();
	return true;
}
