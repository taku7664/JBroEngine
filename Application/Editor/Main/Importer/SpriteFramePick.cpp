#include "pch.h"
#include "SpriteFramePick.h"

#include "Editor/EditorContext.h"
#include "Editor/Main/Importer/SpriteViewerWindow.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/IAssetRegistry.h"

#include <string>

namespace
{
	struct PickState
	{
		AssetGuid     SheetGuid = INVALID_ASSET_GUID;
		File::Guid    RequesterGuid;
		bool          Active    = false;
		bool          HasResult = false;
		std::uint32_t Result    = 0;
	};

	PickState g_state;

	// 뷰어 탭 이름 규칙은 자산 브라우저/인스펙터와 같다 — 확장자까지 붙인 파일명.
	std::string SheetTabTitle(const AssetGuid& sheetGuid)
	{
		SafePtr<IAssetManager> assetManager = EditorContext::GetAssetManager();
		AssetMetaData metaData;
		if (assetManager.IsValid() && assetManager->GetRegistry().TryGetAsset(sheetGuid, metaData))
		{
			return metaData.Path.filename().string();
		}
		return std::string();
	}
}

void SpriteFramePick::Begin(const AssetGuid& sheetGuid, const File::Guid& requesterComponentGuid)
{
	if (sheetGuid.IsNull() || requesterComponentGuid.IsNull())
	{
		return;
	}

	g_state.SheetGuid     = sheetGuid;
	g_state.RequesterGuid = requesterComponentGuid;
	g_state.Active        = true;
	g_state.HasResult     = false;
	g_state.Result        = 0;

	SpriteViewer::Open(sheetGuid, SheetTabTitle(sheetGuid));
}

bool SpriteFramePick::IsWaitingFor(const AssetGuid& sheetGuid)
{
	return g_state.Active && false == g_state.HasResult && g_state.SheetGuid == sheetGuid;
}

bool SpriteFramePick::IsPendingFor(const File::Guid& requesterComponentGuid)
{
	return g_state.Active && g_state.RequesterGuid == requesterComponentGuid;
}

void SpriteFramePick::SetResult(std::uint32_t frameIndex)
{
	if (false == g_state.Active)
	{
		return;
	}
	g_state.HasResult = true;
	g_state.Result    = frameIndex;
}

bool SpriteFramePick::TryTakeResult(const File::Guid& requesterComponentGuid, std::uint32_t& outFrameIndex)
{
	if (false == g_state.Active || false == g_state.HasResult)
	{
		return false;
	}
	if (g_state.RequesterGuid != requesterComponentGuid)
	{
		return false;
	}

	outFrameIndex = g_state.Result;
	Cancel();
	return true;
}

void SpriteFramePick::Cancel()
{
	g_state = PickState{};
}
