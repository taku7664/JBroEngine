#include "pch.h"
#include "AssetManager.h"

#include "Core/Core.h"
#include "Core/Asset/AssetMetaFile.h"
#include "Core/Asset/AssetPath.h"

#include <algorithm>
#include <cwctype>
#include <fstream>

namespace
{
	CAssetManager* GActiveAssetManager = nullptr;
}

bool CAssetManager::Initialize(const AssetManagerDesc& desc)
{
	if (m_isInitialized)
	{
		return true;
	}

	if (false == desc.AssetRootPath.empty())
	{
		m_assetRootPath = File::Path(desc.AssetRootPath.generic_string());
	}

	m_isInitialized = true;
	GActiveAssetManager = this;
	File::SetPathGuidResolver(&CAssetManager::ResolvePathFromActiveRegistry, &CAssetManager::ResolveGuidFromActiveRegistry);
	LoadRegistryFromMetaFiles();
	return true;
}

void CAssetManager::Finalize()
{
	UnloadAllAssets();
	m_packReader.Reset();
	m_loaderTable.clear();
	m_registry.Clear();
	if (GActiveAssetManager == this)
	{
		GActiveAssetManager = nullptr;
		File::SetPathGuidResolver(nullptr, nullptr);
	}
	m_isInitialized = false;
}

bool CAssetManager::RegisterLoader(OwnerPtr<IAssetLoader> loader)
{
	if (!loader)
	{
		return false;
	}

	const EAssetType type = loader->GetSupportedType();
	if (EAssetType::Unknown == type || m_loaderTable.contains(type))
	{
		return false;
	}

	m_loaderTable.emplace(type, std::move(loader));
	return true;
}

IAssetRegistry& CAssetManager::GetRegistry()
{
	return m_registry;
}

const IAssetRegistry& CAssetManager::GetRegistry() const
{
	return m_registry;
}

bool CAssetManager::ImportAsset(const AssetImportDesc& desc, AssetMetaData* outMetaData)
{
#if JBRO_PLATFORM_WEB
	(void)desc;
	(void)outMetaData;
	return false;
#else
	std::string normalizedPath;
	if (EAssetType::Unknown == desc.Type || false == CAssetPath::NormalizeRelativePath(desc.Path.generic_string().c_str(), normalizedPath))
	{
		return false;
	}

	if (CAssetPath::IsMetaPath(normalizedPath.c_str()))
	{
		return false;
	}

	std::lock_guard lock(m_mutex);

	if (const AssetMetaData* registeredMetaData = m_registry.FindAssetByPath(File::Path(normalizedPath)))
	{
		if (nullptr != outMetaData)
		{
			*outMetaData = *registeredMetaData;
		}
		return true;
	}

	AssetMetaData metaData;
	File::Path resolvedMetaPath;
	const std::string metaPath = CAssetPath::MakeMetaPath(normalizedPath.c_str());
	const bool hasResolvedMetaPath = ResolveAssetPath(File::Path(metaPath), resolvedMetaPath);
	if (hasResolvedMetaPath && CAssetMetaFile::Load(resolvedMetaPath, metaData))
	{
		metaData.Path = File::Path(normalizedPath);
		metaData.MetaPath = File::Path(metaPath);
		if (EAssetType::Unknown == metaData.Type)
		{
			metaData.Type = desc.Type;
		}
	}
	else
	{
		metaData.Guid = MakeUniqueAssetGuid();
		metaData.Type = desc.Type;
		metaData.Version = 1;
		metaData.Path = File::Path(normalizedPath);
		metaData.MetaPath = File::Path(metaPath);
		metaData.DisplayName = nullptr != desc.DisplayName ? desc.DisplayName : CAssetPath::GetDisplayNameFromPath(normalizedPath.c_str());
		metaData.Importer = nullptr != desc.Importer ? desc.Importer : "Default";
	}

	if (metaData.Guid.IsNull() || EAssetType::Unknown == metaData.Type)
	{
		return false;
	}

	if (metaData.DisplayName.empty())
	{
		metaData.DisplayName = CAssetPath::GetDisplayNameFromPath(normalizedPath.c_str());
	}

	if (metaData.Importer.empty())
	{
		metaData.Importer = nullptr != desc.Importer ? desc.Importer : "Default";
	}

	if (false == RegisterMetaData(metaData))
	{
		return false;
	}

	if (hasResolvedMetaPath)
	{
		CAssetMetaFile::Save(resolvedMetaPath, metaData);
	}

	if (nullptr != outMetaData)
	{
		*outMetaData = metaData;
	}
	return true;
#endif
}

bool CAssetManager::LoadRegistryFromMetaFiles()
{
#if JBRO_PLATFORM_WEB
	return true;
#else
	std::lock_guard lock(m_mutex);
	// Persistent 항목(외부 ResourceRegistry 등록 자산)은 그대로 두고, 프로젝트 메타만 재구축.
	m_registry.ClearNonPersistent();

	std::error_code errorCode;
	const std::filesystem::path rootPath(m_assetRootPath);
	if (false == std::filesystem::exists(rootPath, errorCode))
	{
		return true;
	}

	if (false == std::filesystem::is_directory(rootPath, errorCode))
	{
		return false;
	}

	for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(rootPath, errorCode))
	{
		if (errorCode)
		{
			return false;
		}

		if (false == entry.is_regular_file(errorCode))
		{
			continue;
		}

		const std::filesystem::path filePath = entry.path();
		std::filesystem::path relativePath = std::filesystem::relative(filePath, rootPath, errorCode);
		if (errorCode)
		{
			errorCode.clear();
			continue;
		}

		std::string relativeMetaPath = relativePath.generic_string();
		std::string normalizedMetaPath;
		if (false == CAssetPath::NormalizeRelativePath(relativeMetaPath.c_str(), normalizedMetaPath))
		{
			continue;
		}

		if (false == CAssetPath::IsMetaPath(normalizedMetaPath.c_str()))
		{
			continue;
		}

		AssetMetaData metaData;
		if (false == CAssetMetaFile::Load(File::Path(filePath), metaData))
		{
			continue;
		}

		std::string assetPath = CAssetPath::StripMetaExtension(normalizedMetaPath.c_str());
		metaData.Path = File::Path(assetPath);
		metaData.MetaPath = File::Path(CAssetPath::MakeMetaPath(assetPath.c_str()));
		RegisterMetaData(metaData);
	}

	return true;
#endif
}

bool CAssetManager::RefreshAssetRegistry()
{
	std::lock_guard lock(m_mutex);
	// Persistent 자산(엔진/에디터 영구 리소스)은 프로젝트 재바인딩 중에도 보존되어야 한다.
	// 보존하지 않으면 ResourceRegistry 가 보유한 SafePtr 가 모두 Alive=false 가 되어 GetSprite 가 invalid 를 반환.
	UnloadNonPersistentAssets();
	return LoadRegistryFromMetaFiles();
}

bool CAssetManager::SetAssetRootPath(const File::Path& assetRootPath)
{
	if (assetRootPath.empty())
	{
		return false;
	}

	std::lock_guard lock(m_mutex);
	m_assetRootPath = File::Path(assetRootPath.generic_string());
	return RefreshAssetRegistry();
}

const File::Path& CAssetManager::GetAssetRootPath() const
{
	return m_assetRootPath;
}

bool CAssetManager::ResolveAssetPath(const File::Path& path, File::Path& outResolvedPath) const
{
	// 절대경로(드라이브 접두사) — 외부 path-based 자산. 그대로 사용 (AssetRoot 와 결합하지 않음).
	if (CAssetPath::IsAbsoluteAssetPath(path.generic_string().c_str()))
	{
		outResolvedPath = File::Path(path.generic_string());
		return true;
	}

	std::string normalizedPath;
	if (false == CAssetPath::NormalizeRelativePath(path.generic_string().c_str(), normalizedPath))
	{
		return false;
	}

	std::filesystem::path resolvedPath(m_assetRootPath);
	resolvedPath /= std::filesystem::path(normalizedPath);
	outResolvedPath = File::Path(resolvedPath.generic_string());
	return true;
}

SafePtr<IAsset> CAssetManager::FindLoadedAsset(const AssetGuid& guid) const
{
	std::lock_guard lock(m_mutex);
	auto it = m_loadedAssetTable.find(guid);
	if (it == m_loadedAssetTable.end())
	{
		return nullptr;
	}

	return it->second.GetSafePtr();
}

SafePtr<IAsset> CAssetManager::LoadAsset(const AssetGuid& guid)
{
	std::lock_guard lock(m_mutex);

	auto it = m_loadedAssetTable.find(guid);
	if (it != m_loadedAssetTable.end())
	{
		return it->second.GetSafePtr();
	}

	const AssetMetaData* metaData = m_registry.FindAsset(guid);
	if (nullptr == metaData)
	{
		return nullptr;
	}

	OwnerPtr<IAsset> asset = LoadAssetInternal(*metaData);
	if (!asset)
	{
		return nullptr;
	}

	SafePtr<IAsset> safeAsset = asset.GetSafePtr();
	m_loadedAssetTable.emplace(guid, std::move(asset));
	return safeAsset;
}

SafePtr<IAsset> CAssetManager::LoadAssetByPath(const File::Path& path)
{
	const AssetMetaData* metaData = m_registry.FindAssetByPath(path);
	if (nullptr == metaData)
	{
		return nullptr;
	}

	return LoadAsset(metaData->Guid);
}

SafePtr<IAsset> CAssetManager::ReloadAsset(const AssetGuid& guid)
{
	std::lock_guard lock(m_mutex);
	UnloadAsset(guid);
	return LoadAsset(guid);
}

SafePtr<IAsset> CAssetManager::ReloadAssetByPath(const File::Path& path)
{
	const AssetMetaData* metaData = m_registry.FindAssetByPath(path);
	if (nullptr == metaData)
	{
		return nullptr;
	}

	return ReloadAsset(metaData->Guid);
}

void CAssetManager::UnloadAsset(const AssetGuid& guid)
{
	std::lock_guard lock(m_mutex);
	auto assetIt = m_loadedAssetTable.find(guid);
	if (assetIt == m_loadedAssetTable.end())
	{
		return;
	}

	if (IAssetLoader* loader = FindLoader(assetIt->second->GetAssetType()))
	{
		loader->Unload(*assetIt->second);
	}

	m_loadedAssetTable.erase(assetIt);
}

bool CAssetManager::UnregisterAssetByPath(const File::Path& path, bool unloadIfLoaded)
{
	std::lock_guard lock(m_mutex);
	const AssetMetaData* metaData = m_registry.FindAssetByPath(path);
	if (nullptr == metaData)
	{
		return false;
	}

	const AssetGuid guid = metaData->Guid;
	if (unloadIfLoaded)
	{
		UnloadAsset(guid);
	}

	return m_registry.UnregisterAsset(guid);
}

bool CAssetManager::MoveAssetPath(const File::Path& oldPath, const File::Path& newPath)
{
	std::lock_guard lock(m_mutex);

	const AssetMetaData* oldMeta = m_registry.FindAssetByPath(oldPath);
	if (nullptr == oldMeta)
	{
		return false;
	}

	// 새 경로에 이미 자산이 등록돼 있으면 — 같은 자산(이미 갱신됨)이면 성공으로,
	// 다른 자산이면 충돌이므로 실패로 처리한다.
	if (const AssetMetaData* existing = m_registry.FindAssetByPath(newPath))
	{
		return existing->Guid == oldMeta->Guid;
	}

	std::string normalizedNewPath;
	if (false == CAssetPath::NormalizeRelativePath(newPath.generic_string().c_str(), normalizedNewPath))
	{
		return false;
	}

	// UnregisterAsset 이후 oldMeta 포인터는 무효해지므로 미리 복사한다.
	AssetMetaData moved = *oldMeta;
	moved.Path     = File::Path(normalizedNewPath);
	moved.MetaPath = File::Path(CAssetPath::MakeMetaPath(normalizedNewPath.c_str()));

	// 레지스트리 항목만 새 경로로 교체한다 — m_loadedAssetTable(로드된 데이터)은
	// 건드리지 않으므로 같은 GUID 로 로드돼 있던 에셋 객체/GPU 리소스가 살아있고,
	// 이를 참조하던 스프라이트/오디오 등 라이브 핸들이 그대로 유효하다.
	if (false == m_registry.UnregisterAsset(moved.Guid))
	{
		return false;
	}
	return m_registry.RegisterAsset(moved);
}

bool CAssetManager::BuildAssetPackage(const AssetPackageBuildDesc& desc)
{
#if JBRO_PLATFORM_WEB
	(void)desc;
	return false;
#else
	const File::Path packPath = false == desc.OutputBlobPath.empty() ? desc.OutputBlobPath : desc.OutputManifestPath;
	if (packPath.empty())
	{
		return false;
	}

	AssetRegistrySnapshot snapshot;
	m_registry.BuildSnapshot(snapshot);

	std::vector<AssetPackageBuildEntry> packageEntries;
	packageEntries.reserve(snapshot.Assets.size());
	for (const AssetMetaData& metaData : snapshot.Assets)
	{
		if (metaData.IsPersistent)
		{
			continue;
		}
		if (metaData.Guid.IsNull() || EAssetType::Unknown == metaData.Type)
		{
			return false;
		}

		File::Path resolvedPath;
		if (false == ResolveAssetPath(metaData.Path, resolvedPath))
		{
			return false;
		}
		std::error_code errorCode;
		if (false == std::filesystem::exists(resolvedPath, errorCode) || false == std::filesystem::is_regular_file(resolvedPath, errorCode))
		{
			return false;
		}

		packageEntries.push_back(AssetPackageBuildEntry{ metaData, resolvedPath });
	}

	if (false == CAssetPackWriter::Write(packPath, packageEntries))
	{
		return false;
	}

	if (false == desc.OutputManifestPath.empty() && desc.OutputManifestPath != packPath)
	{
		std::filesystem::path manifestPath(desc.OutputManifestPath);
		if (manifestPath.has_parent_path())
		{
			std::error_code errorCode;
			std::filesystem::create_directories(manifestPath.parent_path(), errorCode);
			if (errorCode)
			{
				return false;
			}
		}

		std::ofstream manifest(desc.OutputManifestPath, std::ios::binary | std::ios::trunc);
		if (false == manifest.is_open())
		{
			return false;
		}
		manifest << "{\n";
		manifest << "  \"version\": 1,\n";
		manifest << "  \"packs\": [\n";
		manifest << "    { \"id\": \"" << std::filesystem::path(packPath).stem().generic_string()
			<< "\", \"path\": \"" << std::filesystem::path(packPath).filename().generic_string()
			<< "\", \"required\": true }\n";
		manifest << "  ]\n";
		manifest << "}\n";
	}

	return true;
#endif
}

bool CAssetManager::LoadPackedAssetManifest(const File::Path& manifestPath)
{
	if (manifestPath.empty())
	{
		return false;
	}

	OwnerPtr<CAssetPackReader> packReader = MakeOwnerPtr<CAssetPackReader>();
	if (!packReader || false == packReader->Open(manifestPath))
	{
		return false;
	}

	std::vector<AssetRecord> records;
	packReader->BuildRecords(records);
	for (const AssetRecord& record : records)
	{
		AssetMetaData metaData;
		metaData.Guid = record.Guid;
		metaData.Type = record.Type;
		metaData.Version = record.Version;
		metaData.Path = File::Path(std::string("__packed/") + record.Guid.generic_string());
		metaData.MetaPath = File::NULL_PATH;
		metaData.DisplayName = record.Guid.generic_string();
		metaData.Importer = record.Importer;
		metaData.ImportOptionsYaml = record.ImportOptionsYaml;
		metaData.IsPersistent = false;

		if (nullptr != m_registry.FindAsset(metaData.Guid))
		{
			continue;
		}
		if (false == m_registry.RegisterAsset(metaData))
		{
			return false;
		}
	}

	m_packReader = std::move(packReader);
	return true;
}

IAssetLoader* CAssetManager::FindLoader(EAssetType type) const
{
	auto loaderIt = m_loaderTable.find(type);
	if (loaderIt == m_loaderTable.end())
	{
		return nullptr;
	}

	return loaderIt->second.Get();
}

bool CAssetManager::RegisterMetaData(const AssetMetaData& metaData)
{
	if (metaData.Guid.IsNull())
	{
		return false;
	}

	if (nullptr != m_registry.FindAsset(metaData.Guid) || nullptr != m_registry.FindAssetByPath(metaData.Path))
	{
		return false;
	}

	return m_registry.RegisterAsset(metaData);
}

OwnerPtr<IAsset> CAssetManager::LoadAssetInternal(const AssetMetaData& metaData)
{
	IAssetLoader* loader = FindLoader(metaData.Type);
	if (nullptr == loader)
	{
		return nullptr;
	}

	AssetLoadDesc desc;
	desc.Guid = metaData.Guid;
	desc.Type = metaData.Type;
	desc.Path = metaData.Path;
	desc.MetaData = &metaData;

	File::Path resolvedPath;
	const bool hasPackedPayload = m_packReader && m_packReader->ReadPayload(metaData.Guid, desc.MemoryPayload);
	if (false == hasPackedPayload && false == ResolveAssetPath(metaData.Path, resolvedPath))
	{
		return nullptr;
	}
	desc.ResolvedPath = resolvedPath;

	if (!loader->CanLoad(desc) && hasPackedPayload && m_packReader)
	{
		File::Path materializedPath;
		if (m_packReader->MaterializePayload(metaData.Guid, materializedPath))
		{
			desc.ResolvedPath = materializedPath;
		}
	}

	if (!loader->CanLoad(desc))
	{
		return nullptr;
	}

	return loader->Load(desc);
}

AssetGuid CAssetManager::MakeUniqueAssetGuid() const
{
	while (true)
	{
		const AssetGuid guid = CAssetPath::GenerateAssetGuid();
		if (nullptr == m_registry.FindAsset(guid))
		{
			return guid;
		}
	}
}

// path 기반 deterministic GUID — 정규화된 generic_wstring 을 GUID 문자열로 사용.
// File::Guid 는 std::filesystem::path 상속이므로 wide string 그대로 사용 가능.
// 같은 path = 같은 GUID. 일반 import GUID(UUID 형식)와 형태가 달라 충돌 없음.
AssetGuid CAssetManager::MakePathBasedGuid(const File::Path& path)
{
	std::filesystem::path normalized = std::filesystem::path(path).lexically_normal();
	std::wstring text = normalized.generic_wstring();
	// Windows path 대소문자 정규화 — 같은 파일에 대해 GUID 안정성 보장.
	// towlower 는 <cwctype> 의 글로벌 C 함수 (std:: 아님).
	std::transform(text.begin(), text.end(), text.begin(),
		[](wchar_t c) -> wchar_t { return static_cast<wchar_t>(::towlower(c)); });
	return File::Guid(text);
}

bool CAssetManager::RegisterAssetByPath(const File::Path& path, EAssetType type, bool isPersistent)
{
	if (false == m_isInitialized || path.empty() || EAssetType::Unknown == type)
	{
		return false;
	}

	std::lock_guard lock(m_mutex);

	// 이미 등록된 path 면 idempotent — Persistent 플래그만 갱신.
	if (const AssetMetaData* existing = m_registry.FindAssetByPath(path))
	{
		if (existing->IsPersistent != isPersistent)
		{
			return SetAssetPersistent(existing->Guid, isPersistent);
		}
		return true;
	}

	AssetMetaData meta;
	meta.Guid         = MakePathBasedGuid(path);
	meta.Type         = type;
	meta.Version      = 1;
	meta.Path         = path;
	meta.MetaPath     = File::NULL_PATH;   // .Jmeta 디스크 저장 안 함
	meta.DisplayName  = CAssetPath::GetDisplayNameFromPath(path.generic_string().c_str());
	meta.Importer     = "PathOnly";
	meta.IsPersistent = isPersistent;
	return m_registry.RegisterAsset(meta);
}

bool CAssetManager::SetAssetPersistent(const AssetGuid& guid, bool isPersistent)
{
	std::lock_guard lock(m_mutex);
	const AssetMetaData* existing = m_registry.FindAsset(guid);
	if (nullptr == existing)
	{
		return false;
	}
	if (existing->IsPersistent == isPersistent)
	{
		return true;
	}
	// 플래그 변경: snapshot 후 re-register.
	AssetMetaData updated = *existing;
	updated.IsPersistent  = isPersistent;
	m_registry.UnregisterAsset(guid);
	return m_registry.RegisterAsset(updated);
}

void CAssetManager::UnloadNonPersistentAssets()
{
	std::lock_guard lock(m_mutex);
	// 1) 로드된 자산 중 non-persistent 만 unload.
	for (auto it = m_loadedAssetTable.begin(); it != m_loadedAssetTable.end(); )
	{
		const AssetMetaData* meta = m_registry.FindAsset(it->first);
		if (meta && meta->IsPersistent)
		{
			++it;
			continue;
		}

		if (IAssetLoader* loader = FindLoader(it->second->GetAssetType()))
		{
			loader->Unload(*it->second);
		}
		it = m_loadedAssetTable.erase(it);
	}

	// 2) Registry 에서 non-persistent 제거. snapshot 후 순회 (erase 안전성).
	AssetRegistrySnapshot snapshot;
	m_registry.BuildSnapshot(snapshot);
	for (const AssetMetaData& meta : snapshot.Assets)
	{
		if (false == meta.IsPersistent)
		{
			m_registry.UnregisterAsset(meta.Guid);
		}
	}
}

void CAssetManager::UnloadAllAssets()
{
	std::lock_guard lock(m_mutex);
	for (auto& assetPair : m_loadedAssetTable)
	{
		if (IAssetLoader* loader = FindLoader(assetPair.second->GetAssetType()))
		{
			loader->Unload(*assetPair.second);
		}
	}

	m_loadedAssetTable.clear();
}

const File::Path& CAssetManager::ResolvePathFromActiveRegistry(const File::Guid& guid)
{
	if (nullptr == GActiveAssetManager)
	{
		return File::NULL_PATH;
	}

	const AssetMetaData* metaData = GActiveAssetManager->m_registry.FindAsset(guid);
	return metaData ? metaData->Path : File::NULL_PATH;
}

const File::Guid& CAssetManager::ResolveGuidFromActiveRegistry(const File::Path& path)
{
	if (nullptr == GActiveAssetManager)
	{
		return File::NULL_GUID;
	}

	const AssetMetaData* metaData = GActiveAssetManager->m_registry.FindAssetByPath(path);
	return metaData ? metaData->Guid : File::NULL_GUID;
}
