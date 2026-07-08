#pragma once

#include "Core/Asset/IAsset.h"
#include "Core/Asset/IAssetLoader.h"
#include "Core/Renderer/RendererTypes.h"

#include <string>

struct MaterialImportOptions
{
	ERenderQueue Queue = ERenderQueue::Transparent;
};

class CMaterialImportOptions final
{
public:
	static MaterialImportOptions FromYaml(const std::string& yamlText);
	static std::string ToYaml(const MaterialImportOptions& options);
	static const char* QueueToString(ERenderQueue queue);
	static ERenderQueue ParseQueue(const std::string& text);
};

class CMaterialAsset final : public IAsset
{
public:
	explicit CMaterialAsset(const AssetMetaData& metaData);

	AssetGuid GetGuid() const override;
	EAssetType GetAssetType() const override;
	EAssetLoadState GetLoadState() const override;
	const AssetMetaData& GetMetaData() const override;
	void ApplyImportOptions(const std::string& importOptionsYaml) override;

	const MaterialImportOptions& GetImportOptions() const;
	void SetImportOptions(const MaterialImportOptions& options);

	ERenderQueue Queue = ERenderQueue::Transparent;

private:
	AssetMetaData m_metaData;
	MaterialImportOptions m_importOptions;
	EAssetLoadState m_loadState = EAssetLoadState::Loaded;
};

class CMaterialAssetLoader final : public IAssetLoader
{
public:
	EAssetType GetSupportedType() const override;
	bool CanLoad(const AssetLoadDesc& desc) const override;
	OwnerPtr<IAsset> Load(const AssetLoadDesc& desc) override;
	void Unload(IAsset& asset) override;
};
