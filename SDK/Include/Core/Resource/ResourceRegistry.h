#pragma once

#include "Core/Asset/AssetRef.h"
#include "Core/Asset/AssetTypes.h"
#include "Utillity/Pointer/SafePtr.h"

#include <string>
#include <string_view>
#include <unordered_map>

class IAsset;
class CSpriteAsset;
class IAssetManager;

// ── CResourceRegistry ────────────────────────────────────────────────────────
// "Resources/resources.yaml" 매니페스트 기반의 키→자산 매핑.
// Localization 과 유사한 구조 — 코드 내부 문자열 키로 영구 리소스에 접근.
//
// 사용 예:
//   SafePtr<CSpriteAsset> icon = Core::ResourceRegistry->GetSprite("icon-folder");
//   const File::Path&     p    = Core::ResourceRegistry->GetPath  ("icon-folder");
//
// 등록된 모든 자산은 IsPersistent = true 로 마킹되어
// 프로젝트 닫힘(UnloadNonPersistentAssets) 후에도 살아남는다.
class CResourceRegistry final : public EnableSafeFromThis<CResourceRegistry>
{
public:
	// rootDirectory: Resources 폴더 경로 (예: 실행파일 옆 "Resources/")
	// manifestFile : 매니페스트 파일 (보통 "resources.yaml")
	// assetManager : 등록 대상 AssetManager.
	// GPU 텍스처는 첫 사용 시점(RenderResourceCache::AcquireSpriteTexture)에 lazy 생성된다.
	bool Initialize(const File::Path& rootDirectory,
	                const File::Path& manifestFile,
	                SafePtr<IAssetManager> assetManager);
	void Finalize();

	// 키 → 자산. 등록되지 않은 키면 nullptr. ResourceRegistry 가 strong AssetRef 로
	// 보유하므로 반환된 포인터는 ResourceRegistry 가 살아있는 동안 안전.
	IAsset*        Get      (std::string_view key) const;
	CSpriteAsset*  GetSprite(std::string_view key) const;

	// 키 → 절대경로. 등록되지 않은 키면 File::NULL_PATH.
	const File::Path& GetPath(std::string_view key) const;

	bool Has(std::string_view key) const;

private:
	struct Entry
	{
		File::Path       Path;          // 절대경로 (rootDirectory + relative)
		EAssetType       Type = EAssetType::Unknown;
		AssetRef<IAsset> Asset;         // AssetManager 로드 결과 캐시 (strong ref — 영구 보호)
	};

	// 확장자에서 자산 타입 추론.
	static EAssetType InferTypeFromExtension(const File::Path& path);

	std::unordered_map<std::string, Entry> m_entries;
	File::Path                              m_rootDirectory;
	SafePtr<IAssetManager>                  m_assetManager;
};
