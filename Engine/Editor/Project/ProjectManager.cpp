#include "pch.h"
#include "ProjectManager.h"

#include "Core/Asset/AssetPath.h"
#include "Core/Asset/IAssetManager.h"
#include "Core/EngineContext.h"
#include "yaml-cpp/yaml.h"

namespace
{
	constexpr const char* PROJECT_EXTENSION = ".Jproject";
	constexpr const char* PROJECT_KEY_VERSION = "Version";
	constexpr const char* PROJECT_KEY_ROOT_PATH = "RootPath";
	constexpr const char* ASSETS_DIRECTORY_NAME = "Assets";
}

bool CProjectManager::Initialize(const EngineContext& context)
{
	m_assetManager = context.AssetManager;
	m_assetWatcher = MakeOwnerPtr<CWindowsFileWatcher>();
	m_info.OriginPath = File::Path(std::filesystem::current_path());
	m_isInitialized = m_assetManager.IsValid() && static_cast<bool>(m_assetWatcher);
	return m_isInitialized;
}

void CProjectManager::Finalize()
{
	CloseProject();
	m_assetWatcher.Reset();
	m_assetManager = nullptr;
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

	m_info.Version = version;
	m_info.ProjectFilePath = File::Path(projectPath);
	m_info.RootPath = File::Path(projectRootPath);
	m_info.AssetPath = File::Path(assetPath);

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
	return true;
}

void CProjectManager::CloseProject()
{
	if (m_assetWatcher)
	{
		m_assetWatcher->Stop();
	}
	m_isProjectLoaded = false;
	m_info.ProjectFilePath = File::NULL_PATH;
	m_info.RootPath = File::NULL_PATH;
	m_info.AssetPath = File::NULL_PATH;
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
		for (const FileWatchEvent& event : events)
		{
			ProcessAssetEvent(event);
		}
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

void CProjectManager::ProcessAssetEvent(const FileWatchEvent& event)
{
	if (false == m_assetManager.IsValid())
	{
		return;
	}

	if (EFileWatchEventType::Deleted == event.Type)
	{
		m_assetManager->RefreshAssetRegistry();
		return;
	}

	ImportOrReloadAsset(event.Path);
}

void CProjectManager::ImportOrReloadAsset(const File::Path& absolutePath)
{
	if (false == IsImportableAssetPath(absolutePath))
	{
		return;
	}

	std::string relativePath;
	if (false == MakeAssetRelativePath(absolutePath, relativePath))
	{
		return;
	}

	const EAssetType type = DetectAssetType(relativePath);
	if (EAssetType::Unknown == type)
	{
		return;
	}

	AssetImportDesc importDesc;
	importDesc.Type = type;
	importDesc.Path = relativePath.c_str();
	importDesc.Importer = "Default";
	AssetMetaData metaData;
	if (m_assetManager->ImportAsset(importDesc, &metaData))
	{
		m_assetManager->ReloadAsset(metaData.Guid);
	}
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
