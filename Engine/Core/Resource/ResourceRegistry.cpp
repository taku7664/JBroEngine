#include "pch.h"
#include "ResourceRegistry.h"

#include "Core/Asset/IAssetManager.h"
#include "Core/Asset/SpriteAsset.h"
#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <cctype>

namespace
{
	EAssetType ParseTypeString(const std::string& s)
	{
		if (s == "Sprite")    return EAssetType::Sprite;
		if (s == "Mesh")      return EAssetType::Mesh;
		if (s == "Material")  return EAssetType::Material;
		if (s == "Shader")    return EAssetType::Shader;
		if (s == "Scene")     return EAssetType::Scene;
		if (s == "Prefab")    return EAssetType::Prefab;
		if (s == "Script")    return EAssetType::Script;
		if (s == "Custom")    return EAssetType::Custom;
		return EAssetType::Unknown;
	}
}

EAssetType CResourceRegistry::InferTypeFromExtension(const File::Path& path)
{
	std::string ext = std::filesystem::path(path).extension().generic_string();
	std::transform(ext.begin(), ext.end(), ext.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if (ext == ".png" || ext == ".jpg" || ext == ".jpeg"
	    || ext == ".bmp" || ext == ".tga")            return EAssetType::Sprite;
	if (ext == ".jscene")                              return EAssetType::Scene;
	if (ext == ".jprefab")                             return EAssetType::Prefab;
	if (ext == ".jmat")                                return EAssetType::Material;
	if (ext == ".hlsl" || ext == ".shader")            return EAssetType::Shader;
	if (ext == ".wav" || ext == ".mp3"
	    || ext == ".flac" || ext == ".ogg")            return EAssetType::Audio;
	return EAssetType::Custom;
}

bool CResourceRegistry::Initialize(const File::Path& rootDirectory,
                                   const File::Path& manifestFile,
                                   SafePtr<IAssetManager> assetManager)
{
	if (false == assetManager.IsValid())
	{
		return false;
	}

	m_rootDirectory = rootDirectory;
	m_assetManager  = assetManager;

	const std::filesystem::path manifestPath = std::filesystem::path(rootDirectory) / std::filesystem::path(manifestFile);
	std::error_code ec;
	if (false == std::filesystem::exists(manifestPath, ec))
	{
		return false;
	}

	YAML::Node root;
	try { root = YAML::LoadFile(manifestPath.string()); }
	catch (const YAML::Exception&) { return false; }

	const YAML::Node resources = root["resources"];
	if (!resources || false == resources.IsMap())
	{
		return false;
	}

	for (auto it = resources.begin(); it != resources.end(); ++it)
	{
		const std::string key = it->first.as<std::string>();
		const YAML::Node  val = it->second;

		// 값 형식: { path: "...", type: "Sprite" }  또는 그냥 문자열(path).
		File::Path relPath;
		EAssetType type = EAssetType::Unknown;

		if (val.IsScalar())
		{
			relPath = File::Path(val.as<std::string>());
		}
		else if (val.IsMap())
		{
			if (val["path"]) relPath = File::Path(val["path"].as<std::string>());
			if (val["type"]) type    = ParseTypeString(val["type"].as<std::string>());
		}
		if (relPath.empty()) continue;

		// 매니페스트의 path 는 Resources 폴더 부모 기준 또는 자체 기준 둘 다 허용.
		// 예: "Resources/folder.png" (포함) 또는 "folder.png" (제외).
		// AssetManager 가 외부 자산을 식별할 수 있도록 반드시 절대경로로 정규화.
		std::filesystem::path absPath(relPath);
		if (false == absPath.is_absolute())
		{
			std::filesystem::path rootParent = std::filesystem::path(m_rootDirectory).parent_path();
			if (rootParent.empty())
			{
				// rootDirectory 가 단일 컴포넌트("Resources")라 부모가 비어있음 → cwd 기준.
				rootParent = std::filesystem::current_path();
			}
			absPath = rootParent / absPath;
		}
		std::error_code absEc;
		std::filesystem::path canonical = std::filesystem::absolute(absPath, absEc);
		if (!absEc)
		{
			absPath = canonical;
		}

		// type 누락 시 확장자에서 추론.
		if (EAssetType::Unknown == type)
		{
			type = InferTypeFromExtension(File::Path(absPath));
		}

		Entry entry;
		entry.Path = File::Path(absPath);
		entry.Type = type;

		// AssetManager 에 persistent 등록 + 즉시 로드.
		if (m_assetManager->RegisterAssetByPath(entry.Path, type, /*isPersistent*/ true))
		{
			entry.Asset = m_assetManager->LoadAssetByPath(entry.Path);
		}

		// GPU 텍스처는 첫 사용 시점에 RenderResourceCache 가 lazy 생성한다 (여기서는 등록만).

		m_entries.emplace(std::move(key), std::move(entry));
	}

	return false == m_entries.empty();
}

void CResourceRegistry::Finalize()
{
	m_entries.clear();
	m_assetManager.Reset();
	m_rootDirectory = File::NULL_PATH;
}

IAsset* CResourceRegistry::Get(std::string_view key) const
{
	auto it = m_entries.find(std::string(key));
	return it != m_entries.end() ? it->second.Asset.Get() : nullptr;
}

CSpriteAsset* CResourceRegistry::GetSprite(std::string_view key) const
{
	IAsset* asset = Get(key);
	if (nullptr == asset) return nullptr;
	if (EAssetType::Sprite != asset->GetAssetType()) return nullptr;
	return static_cast<CSpriteAsset*>(asset);
}

const File::Path& CResourceRegistry::GetPath(std::string_view key) const
{
	auto it = m_entries.find(std::string(key));
	return it != m_entries.end() ? it->second.Path : File::NULL_PATH;
}

bool CResourceRegistry::Has(std::string_view key) const
{
	return m_entries.find(std::string(key)) != m_entries.end();
}
