#pragma once

#include "Engine/Core/Asset/AssetTypes.h"
#include "Engine/Core/Asset/SpriteAsset.h"
#include "Engine/Core/Asset/TransientAssetLoad.h"
#include "Engine/Editor/ImWindow/ImCustomWindow.h"

#include <string>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CSpriteSheetViewerWindow ─ 스프라이트 시트를 격자와 함께 보는 창
//
//  왜 필요한가: 슬라이스 옵션은 지금까지 **숫자만** 보였다(프레임 수 / 첫 프레임 크기).
//  마진·갭·셀 크기를 맞추는 건 눈 감고 하는 작업이었다.
//
//  옵션을 **소유하지 않는다.** 편집 주체는 인스펙터의 임포트 옵션 섹션이고, 이 창은
//  매 프레임 그 값을 받아(PushOptions) 격자를 다시 그린다. 슬라이더를 움직이면 격자가
//  즉시 따라오고, 저장(Apply)은 여전히 인스펙터가 한다 —
//  같은 값을 두 곳에서 편집하면 어느 쪽이 진짜인지 알 수 없어진다.
//
//  자산은 창이 열려 있는 동안 CTransientAssetLoad 로 붙잡는다. 자산 캐시에 자동 GC 가
//  없어서, 그냥 LoadAsset 하면 열어 본 시트가 전부 메모리에 남는다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
class CSpriteSheetViewerWindow final : public CImCustomWindow
{
public:
	using CImCustomWindow::CImCustomWindow;

	void SetTargetGuid(const AssetGuid& guid) { m_guid = guid; }
	const AssetGuid& GetTargetGuid() const { return m_guid; }

	// 인스펙터가 편집 중인 옵션을 밀어 넣는다. 창이 그리기 전에 매 프레임 호출된다.
	void SetOptions(const SpriteImportOptions& options) { m_options = options; }

private:
	void OnRenderStay() override;

	// 창을 닫으면 붙잡고 있던 시트를 내린다. 파괴가 아니라 숨김이라 소멸자만 믿으면
	// 열어 본 시트가 에디터가 꺼질 때까지 메모리에 남는다(자산 캐시에 자동 GC 없음).
	void OnHide() override { m_assetLoad.Release(); }

	void DrawToolbar(float textureWidth, float textureHeight);
	void DrawSheet(const CSpriteAsset& spriteAsset);

	AssetGuid            m_guid = INVALID_ASSET_GUID;
	SpriteImportOptions  m_options;
	CTransientAssetLoad  m_assetLoad;

	float m_zoom = 1.0f;
	bool  m_showPivot = true;
	int   m_hoveredFrame = -1;
};

namespace SpriteSheetViewer
{
	// 시트 뷰어를 연다. 이미 열려 있으면 Focus 만 한다.
	// 윈도우 draw/순회 도중에도 안전하도록 생성을 ImEditor::QueueDeferred 로 지연한다.
	void Open(const AssetGuid& guid, const std::string& title);

	// 편집 중인 옵션을 열려 있는 뷰어에 전달한다. 안 열려 있으면 아무 일도 하지 않는다.
	void PushOptions(const AssetGuid& guid, const SpriteImportOptions& options);
}
