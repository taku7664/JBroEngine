#pragma once

#include "Core/Asset/IAsset.h"
#include "Core/Asset/IAssetLoader.h"

// 파일 바이트를 그대로 들고 있는 자산. 내용 해석은 사용자 몫이다(GetText/GetData).
// 파생 = "같은 파일 자산이지만 별개의 종류" — 종류를 타입으로 구분해야 Ref<T> 가
// 그 종류만 받는다(예: CCanvasAsset). 파생은 신원만 더하고 적재 경로는 공유한다.
class CFileAsset : public IAsset
{
public:
	CFileAsset(const AssetMetaData& metaData, std::vector<std::uint8_t>&& data);
	~CFileAsset() override = default;

	AssetGuid GetGuid() const override;
	EAssetType GetAssetType() const override;
	EAssetLoadState GetLoadState() const override;
	const AssetMetaData& GetMetaData() const override;

	const std::vector<std::uint8_t>& GetData() const;
	std::string_view GetText() const;

private:
	AssetMetaData m_metaData;
	std::vector<std::uint8_t> m_data;
	EAssetLoadState m_loadState = EAssetLoadState::Loaded;
};

class CFileAssetLoader : public IAssetLoader
{
public:
	explicit CFileAssetLoader(EAssetType supportedType);
	~CFileAssetLoader() override = default;

	EAssetType GetSupportedType() const override;
	bool CanLoad(const AssetLoadDesc& desc) const override;
	OwnerPtr<IAsset> Load(const AssetLoadDesc& desc) override;
	void Unload(IAsset& asset) override;

protected:
	// 읽어들인 바이트로 자산 객체를 만든다. 파생 로더가 여기만 바꿔 자기 타입을 내놓는다 —
	// 파일 읽기(메모리 페이로드/디스크 분기, 실패 처리)는 Load 에 한 벌만 둔다.
	virtual OwnerPtr<IAsset> CreateAsset(const AssetMetaData& metaData, std::vector<std::uint8_t>&& data) const;

private:
	EAssetType m_supportedType = EAssetType::Unknown;
};
