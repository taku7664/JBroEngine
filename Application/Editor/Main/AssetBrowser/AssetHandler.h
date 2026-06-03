#pragma once
#include "AssetBrowserTool.h"

class CDefaultAssetOpenHandler final : public CAssetBrowserTool::IAssetOpenHandler
{
public:
	bool CanOpen(const AssetBrowserEntry& entry) const override;
	void Open(CAssetBrowserTool& browser, const AssetBrowserEntry& entry) override;
};


class CSceneAssetOpenHandler final : public CAssetBrowserTool::IAssetOpenHandler
{
public:
	bool CanOpen(const AssetBrowserEntry& entry) const override;
	void Open(CAssetBrowserTool&, const AssetBrowserEntry& entry) override;
};


// 스크립트 소스 파일(.cpp/.h/.hpp) 또는 EAssetType::Script 자산을 더블클릭하면
// ProjectManager 를 통해 Visual Studio 프로젝트만 연다.
// 소스 파일 자체는 열지 않는다.
class CScriptAssetOpenHandler final : public CAssetBrowserTool::IAssetOpenHandler
{
public:
	bool CanOpen(const AssetBrowserEntry& entry) const override;
	void Open(CAssetBrowserTool&, const AssetBrowserEntry& entry) override;
};


// 사운드 효과(.jfx / EAssetType::AudioEffect) 더블클릭 시 전용 에디터 독윈도우를
// 띄운다. 이미 열려 있으면 새로 만들지 않고 그 창을 Focus 한다.
class CEffectAssetOpenHandler final : public CAssetBrowserTool::IAssetOpenHandler
{
public:
	bool CanOpen(const AssetBrowserEntry& entry) const override;
	void Open(CAssetBrowserTool&, const AssetBrowserEntry& entry) override;
};
