#pragma once

#include "Engine/Core/Asset/AssetTypes.h"
#include "Engine/Core/Asset/SpriteAsset.h"
#include "Engine/Core/Asset/TransientAssetLoad.h"
#include "Engine/Editor/ImWindow/ImCustomWindow.h"
#include "Engine/Editor/ImWindow/ImDockWindow.h"

#include <string>
#include <vector>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  스프라이트 뷰어 ─ 시트를 격자와 함께 보고, 프레임을 골라 애니메이션으로 돌려 본다.
//
//  구조는 사운드 효과 에디터와 같다 — **파일당 dock + panel 한 쌍**.
//    Root → CSpriteViewerDockWindow("스프라이트 뷰어") → CSpriteViewerPanel(파일명)
//
//  화면 배치
//    ┌──────────────────────────┬──────────────────┐
//    │  시트 + 슬라이스 격자      │  미리보기         │
//    │  (칸 클릭 = 프레임 선택)   ├──────────────────┤
//    │                          │  임포트 옵션      │
//    └──────────────────────────┴──────────────────┘
//      프레임 [====|====]  재생  FPS
//
//  임포트 옵션은 **SpriteImportOptionsEditor 가 소유한다** — 인스펙터와 이 창이 같은 값을
//  편집한다. 각자 복사본을 들면 한쪽 편집이 다른 쪽에서 조용히 사라진다.
//
//  "프레임만 보는 모드"를 따로 두지 않는다. 미리보기 칸이 그 역할을 하므로 모드 전환 없이
//  슬라이더와 재생 버튼만으로 충분하다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// 실제 뷰어 UI 를 담는 내부 패널(도킹 자식). 탭 이름이 파일명이다.
class CSpriteViewerPanel final : public CImCustomWindow
{
public:
	using CImCustomWindow::CImCustomWindow;

	void SetTargetGuid(const AssetGuid& guid) { m_guid = guid; }
	const AssetGuid& GetTargetGuid() const { return m_guid; }

private:
	void OnRenderStay() override;

	// 창을 닫으면 붙잡고 있던 시트를 내린다. 파괴가 아니라 숨김이라 소멸자만 믿으면
	// 열어 본 시트가 에디터가 꺼질 때까지 메모리에 남는다(자산 캐시에 자동 GC 없음).
	void OnHide() override { m_assetLoad.Release(); }

	void DrawToolbar(float textureWidth, float textureHeight);
	void DrawSheetPane(const CSpriteAsset& spriteAsset, const std::vector<SpriteFrame>& frames, const ImVec2& paneSize);
	void DrawSidePane(const AssetMetaData& metaData, const CSpriteAsset& spriteAsset,
	                  const std::vector<SpriteFrame>& frames, const ImVec2& paneSize);
	void DrawPreview(const CSpriteAsset& spriteAsset, const std::vector<SpriteFrame>& frames, float paneWidth);
	void DrawPlaybackBar(const std::vector<SpriteFrame>& frames);
	void AdvancePlayback(std::size_t frameCount);

	AssetGuid           m_guid = INVALID_ASSET_GUID;
	CTransientAssetLoad m_assetLoad;

	float m_splitRatio = 0.68f;
	float m_zoom = 1.0f;
	// 미리보기 배율 — 칸 맞춤 배율에 곱한다(1.0 = 칸에 꽉 차게). Ctrl+휠로 조절.
	float m_previewZoom = 1.0f;
	bool  m_showPivot = true;

	int   m_hoveredFrame = -1;
	int   m_selectedFrame = 0;

	bool  m_playing = false;
	float m_framesPerSecond = 12.0f;
	float m_elapsedSeconds = 0.0f;
};

// 파일당 하나씩 뜨는 도킹 컨테이너. Root 의 자식.
class CSpriteViewerDockWindow final : public CImDockWindow
{
public:
	using CImDockWindow::CImDockWindow;
};

namespace SpriteViewer
{
	// 스프라이트 자산(guid) 전용 뷰어를 연다. 이미 열려 있으면 Focus 만.
	// 윈도우 draw/순회 도중에도 안전하도록 생성을 ImEditor::QueueDeferred 로 지연한다.
	void Open(const AssetGuid& guid, const std::string& title);
}
