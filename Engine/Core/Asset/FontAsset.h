#pragma once

#include "Core/Asset/IAsset.h"
#include "Core/Asset/IAssetLoader.h"

#include <cstdint>
#include <vector>

enum class EFontStyle : std::uint8_t
{
	Regular,
	Bold,
	Italic,
	BoldItalic
};

class CFontFaceAsset final : public IAsset
{
public:
	CFontFaceAsset(const AssetMetaData& metaData, std::vector<std::uint8_t>&& bytes, std::uint32_t faceIndex);

	AssetGuid GetGuid() const override;
	// Ref<T> 가 RTTI 없이 타입을 확인하는 컴파일타임 짝 — 로더 등록이 지키는
	// "이 타입은 이 클래스" 계약을 코드로 적어 둔 것이다.
	static constexpr EAssetType StaticAssetType() { return EAssetType::FontFace; }

	EAssetType GetAssetType() const override;
	EAssetLoadState GetLoadState() const override;
	const AssetMetaData& GetMetaData() const override;
	const std::vector<std::uint8_t>& GetBytes() const;
	std::uint32_t GetFaceIndex() const;
	std::uint32_t GetGeneration() const;

	// 재임포트 in-place 갱신 — 같은 객체에 새 폰트 바이트를 덮어쓰고 generation 을 올린다.
	// (CSpriteAsset::ReplacePixels 와 동일 모델. 외부 AssetRef 가 살아남아 소비자가 generation
	//  비교로 무효화를 감지한다. FT_Face 는 이 바이트 버퍼를 참조하므로 소비자가 반드시 재구축해야 한다.)
	void ReplaceBytes(std::vector<std::uint8_t>&& bytes, std::uint32_t faceIndex);

private:
	AssetMetaData m_metaData;
	std::vector<std::uint8_t> m_bytes;
	std::uint32_t m_faceIndex = 0;
	std::uint32_t m_generation = 1;
	EAssetLoadState m_loadState = EAssetLoadState::Loaded;
};

struct FontFamilyData
{
	AssetGuid Regular = INVALID_ASSET_GUID;
	AssetGuid Bold = INVALID_ASSET_GUID;
	AssetGuid Italic = INVALID_ASSET_GUID;
	AssetGuid BoldItalic = INVALID_ASSET_GUID;
	bool UseProjectFallbacks = true;
	std::vector<AssetGuid> FallbackFamilies;
};

class CFontFamilyAsset final : public IAsset
{
public:
	CFontFamilyAsset(const AssetMetaData& metaData, FontFamilyData data);

	AssetGuid GetGuid() const override;
	// Ref<T> 가 RTTI 없이 타입을 확인하는 컴파일타임 짝 — 로더 등록이 지키는
	// "이 타입은 이 클래스" 계약을 코드로 적어 둔 것이다.
	static constexpr EAssetType StaticAssetType() { return EAssetType::FontFamily; }

	EAssetType GetAssetType() const override;
	EAssetLoadState GetLoadState() const override;
	const AssetMetaData& GetMetaData() const override;
	const FontFamilyData& GetData() const;
	std::uint32_t GetGeneration() const;

	// 재임포트 in-place 갱신 — 같은 객체에 새 데이터를 덮어쓰고 generation 을 올린다.
	// (CFontFaceAsset::ReplaceBytes 와 같은 모델. 외부 AssetRef 가 살아남고, 소비자는
	//  generation 비교로 무효화를 감지한다 — 패밀리는 폴백 목록이 바뀌어도 guid 가 그대로라
	//  이 값이 없으면 텍스트가 옛 face 로 계속 렌더된다.)
	void SetData(FontFamilyData data);
	AssetGuid ResolveFace(EFontStyle style, bool& usedRegularFallback) const;

	// 빌드 패키징용: 폰트 패밀리 파일을 로드하지 않고 의존 자산 GUID 만 읽는다.
	// 반환: 스타일별 FontFace(Regular/Bold/Italic/BoldItalic) + FallbackFamilies(패밀리, 전이 대상).
	// (수집기가 FallbackFamilies 를 다시 이 함수로 전개한다.)
	static std::vector<AssetGuid> ReadDependencyGuids(const File::Path& path);

private:
	AssetMetaData m_metaData;
	FontFamilyData m_data;
	std::uint32_t m_generation = 1;
	EAssetLoadState m_loadState = EAssetLoadState::Loaded;
};

class CFontFaceAssetLoader final : public IAssetLoader
{
public:
	EAssetType GetSupportedType() const override;
	bool CanLoad(const AssetLoadDesc& desc) const override;
	OwnerPtr<IAsset> Load(const AssetLoadDesc& desc) override;
	void Unload(IAsset& asset) override;
	bool ReloadInto(IAsset& existing, const AssetMetaData& metaData) override;
};

class CFontFamilyAssetLoader final : public IAssetLoader
{
public:
	EAssetType GetSupportedType() const override;
	bool CanLoad(const AssetLoadDesc& desc) const override;
	OwnerPtr<IAsset> Load(const AssetLoadDesc& desc) override;
	void Unload(IAsset& asset) override;
	bool ReloadInto(IAsset& existing, const AssetMetaData& metaData) override;
};
