#include "pch.h"
#include "SpriteViewerWindow.h"

#include "Editor/Editor.h"
#include "Editor/EditorContext.h"
#include "Editor/ImItem/ImSectionHeader.h"
#include "Editor/ImItem/ImSplitter.h"
#include "Editor/Localization/EditorLocalizationKeys.h"
#include "Editor/Main/Importer/SpriteImportOptionsEditor.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Core/Localization/LocalizationManager.h"
#include "Engine/Core/Renderer/IRenderResourceCache.h"
#include "Engine/Core/RHI/IRHITexture.h"
#include "Engine/Editor/ImEditor.h"
#include "ThirdParty/imgui/imgui.h"

#include <algorithm>
#include <cstdio>

namespace
{
	constexpr float ZOOM_MIN = 0.05f;
	constexpr float ZOOM_MAX = 16.0f;
	constexpr float SPLITTER_WIDTH = 4.0f;
	constexpr float SPLIT_MIN_RATIO = 0.25f;
	constexpr float SPLIT_MAX_RATIO = 0.85f;

	// 셀이 이보다 작으면 인덱스 라벨을 그리지 않는다 — 겹쳐서 격자만 더럽힌다.
	constexpr float LABEL_MIN_CELL_PIXELS = 22.0f;

	// 한 프레임에 애니메이션이 건너뛸 수 있는 최대 프레임 수. 창을 오래 멈춰 뒀다가
	// 돌아왔을 때(브레이크포인트·드래그) 델타가 커져도 폭주하지 않게 막는다.
	constexpr int MAX_PLAYBACK_ADVANCE = 240;

	const ImU32 GRID_COLOR     = IM_COL32(80, 200, 255, 180);
	const ImU32 HOVER_COLOR    = IM_COL32(255, 220, 90, 255);
	const ImU32 SELECTED_COLOR = IM_COL32(120, 255, 140, 255);
	const ImU32 PIVOT_COLOR    = IM_COL32(255, 110, 110, 230);
	const ImU32 LABEL_COLOR    = IM_COL32(255, 255, 255, 200);

	// 도킹 컨테이너는 **하나만** 둔다. 파일마다 새 dock 을 만들면 "스프라이트 뷰어" 탭이
	// 여러 개 생겨 버린다 — 원하는 건 뷰어 하나에 파일 탭이 여러 개 붙는 형태다.
	constexpr const char* SHARED_DOCK_KEY = "SpriteViewerDock";

	std::string PanelKey(const AssetGuid& guid) { return std::string("SpriteViewerPanel_") + guid.generic_string(); }

	ImTextureID AcquireTextureId(const AssetGuid& guid, CSpriteAsset& spriteAsset)
	{
		if (false == Engine.RenderResourceCache.IsValid())
		{
			return 0;
		}
		SafePtr<IRHITexture> texture = Engine.RenderResourceCache->AcquireSpriteTexture(guid, spriteAsset);
		if (false == texture.IsValid())
		{
			return 0;
		}
		return reinterpret_cast<ImTextureID>(texture->GetNativeHandle().ShaderResourceView);
	}

	// 피벗은 **Y-up**(0=아래, 1=위)이고 텍스처 좌표는 Y-down 이라 세로를 뒤집어 찍는다.
	void DrawPivotCross(ImDrawList& drawList, const ImVec2& topLeft, const ImVec2& size, float pivotX, float pivotY)
	{
		const float x = topLeft.x + pivotX * size.x;
		const float y = topLeft.y + (1.0f - pivotY) * size.y;
		drawList.AddLine(ImVec2(x - 5.0f, y), ImVec2(x + 5.0f, y), PIVOT_COLOR, 1.5f);
		drawList.AddLine(ImVec2(x, y - 5.0f), ImVec2(x, y + 5.0f), PIVOT_COLOR, 1.5f);
	}

	bool TryGetMetaData(const AssetGuid& guid, AssetMetaData& outMetaData)
	{
		SafePtr<IAssetManager> assetManager = EditorContext::GetAssetManager();
		return assetManager.IsValid() && assetManager->GetRegistry().TryGetAsset(guid, outMetaData);
	}
}

void SpriteViewer::Open(const AssetGuid& guid, const std::string& title)
{
	if (false == Editor::ImEditor.IsValid() || false == Editor::RootDockWindow.IsValid() || guid.IsNull())
	{
		return;
	}

	// 버튼 클릭은 윈도우 draw/순회 도중 발생한다. 여기서 바로 CreateImWindow 하면
	// 윈도우 벡터가 재할당되어 순회 중 반복자가 무효화된다 → 다음 Update 끝으로 지연.
	Editor::ImEditor->QueueDeferred([guid, title]()
	{
		if (false == Editor::ImEditor.IsValid() || false == Editor::RootDockWindow.IsValid())
		{
			return;
		}

		const ImGuiID dockId  = ImHashStr(SHARED_DOCK_KEY);
		const ImGuiID panelId = ImHashStr(PanelKey(guid).c_str());

		// 공용 dock 을 먼저 확보한다. 이미 있으면 그대로 쓰고, 없을 때만 만든다.
		SafePtr<CImWindow> dock = DynamicSafePtrCast<CImWindow>(Editor::ImEditor->FindImWindow(dockId));
		if (false == dock.IsValid())
		{
			dock = DynamicSafePtrCast<CImWindow>(
				Editor::ImEditor->CreateImWindow<CSpriteViewerDockWindow>(SHARED_DOCK_KEY, Editor::RootDockWindow->GetID()));
			if (false == dock.IsValid())
			{
				return;
			}
			// Dock 제목은 파일명이 아니라 "스프라이트 뷰어" 로 고정한다(언어 전환도 반영).
			// 파일명은 안쪽 패널 탭이 들고 있다 — 사운드 효과 에디터와 같은 규칙.
			dock->SetLocalizedTitleKey(EditorLocKeys::SpriteViewerTitle);
			dock->SetSize(ImVec2(960.0f, 640.0f));
		}
		dock->SetVisible(true);

		// 이 파일의 탭이 이미 있으면 그걸 앞으로 가져온다.
		if (SafePtr<CSpriteViewerPanel> existing =
			DynamicSafePtrCast<CSpriteViewerPanel>(Editor::ImEditor->FindImWindow(panelId)))
		{
			existing->SetVisible(true);
			existing->Focus();
			return;
		}

		SafePtr<CSpriteViewerPanel> panel =
			Editor::ImEditor->CreateImWindow<CSpriteViewerPanel>(PanelKey(guid).c_str(), dockId);
		if (panel.IsValid())
		{
			panel->SetTitle(title.c_str());
			panel->SetTargetGuid(guid);
			panel->Focus();
		}
	});
}

void CSpriteViewerPanel::OnRenderStay()
{
	AssetMetaData metaData;
	if (false == TryGetMetaData(m_guid, metaData))
	{
		ImGui::TextUnformatted(Loc::Text(EditorLocKeys::SpriteViewerNoTexture));
		return;
	}

	SafePtr<IAssetManager> assetManager = EditorContext::GetAssetManager();
	CSpriteAsset* spriteAsset = nullptr;
	if (assetManager)
	{
		// 창이 열려 있는 동안만 붙잡는다(OnHide 에서 놓는다).
		if (const AssetRef<IAsset>& asset = m_assetLoad.Acquire(*assetManager, m_guid))
		{
			if (EAssetType::Sprite == asset->GetAssetType())
			{
				spriteAsset = static_cast<CSpriteAsset*>(asset.Get());
			}
		}
	}

	if (nullptr == spriteAsset || 0 == spriteAsset->GetWidth() || 0 == spriteAsset->GetHeight())
	{
		ImGui::TextUnformatted(Loc::Text(EditorLocKeys::SpriteViewerNoTexture));
		return;
	}

	// 격자는 **편집 중인 옵션**으로 계산한다. 자산이 들고 있는 프레임은 마지막으로 Apply 된
	// 결과라, 슬라이더를 움직이는 동안에는 화면과 어긋난다.
	const SpriteImportOptions& options = SpriteImportOptionsEditor::Get(metaData);
	const std::vector<SpriteFrame> frames =
		CSpriteImportOptions::BuildFrames(spriteAsset->GetWidth(), spriteAsset->GetHeight(), options);

	// 다시 잘라 프레임 수가 줄면 선택/재생 인덱스가 범위를 벗어난다.
	m_selectedFrame = frames.empty()
		? 0
		: std::clamp(m_selectedFrame, 0, static_cast<int>(frames.size()) - 1);

	DrawToolbar(static_cast<float>(spriteAsset->GetWidth()), static_cast<float>(spriteAsset->GetHeight()));
	ImGui::Separator();

	// 아래쪽 재생 바 높이를 빼고 본문을 나눈다.
	const float playbackHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
	const ImVec2 totalAvail = ImGui::GetContentRegionAvail();
	const ImVec2 bodyAvail(totalAvail.x, std::max(totalAvail.y - playbackHeight, 1.0f));

	const float leftWidth = bodyAvail.x * m_splitRatio - SPLITTER_WIDTH * 0.5f;
	const float rightWidth = bodyAvail.x - leftWidth - SPLITTER_WIDTH;

	DrawSheetPane(*spriteAsset, frames, ImVec2(leftWidth, bodyAvail.y));

	ImGui::Utillity::VerticalSplitter("##SpriteViewerSplitter", m_splitRatio, bodyAvail,
		SPLIT_MIN_RATIO, SPLIT_MAX_RATIO, SPLITTER_WIDTH);

	DrawSidePane(metaData, *spriteAsset, frames, ImVec2(rightWidth, bodyAvail.y));

	DrawPlaybackBar(frames);
}

void CSpriteViewerPanel::DrawToolbar(float textureWidth, float textureHeight)
{
	ImGui::SetNextItemWidth(160.0f);
	ImGui::SliderFloat(Loc::Text(EditorLocKeys::SpriteViewerZoom), &m_zoom, ZOOM_MIN, ZOOM_MAX, "%.2fx");

	ImGui::SameLine();
	if (ImGui::Button(Loc::Text(EditorLocKeys::SpriteViewerFit)))
	{
		// 시트 칸에 남을 크기에 맞춘다. 폰트/DPI 가 바뀌어도 따라가도록 상수 대신
		// ImGui 의 실제 줄 높이를 쓴다.
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const float paneWidth = available.x * m_splitRatio;
		const float paneHeight = available.y - ImGui::GetFrameHeightWithSpacing() * 2.0f;
		const float fitX = textureWidth > 0.0f ? paneWidth / textureWidth : 1.0f;
		const float fitY = textureHeight > 0.0f ? paneHeight / textureHeight : 1.0f;
		m_zoom = std::clamp(std::min(fitX, fitY), ZOOM_MIN, ZOOM_MAX);
	}

	ImGui::SameLine();
	ImGui::Checkbox(Loc::Text(EditorLocKeys::SpriteViewerShowPivot), &m_showPivot);

	ImGui::SameLine();
	ImGui::TextDisabled("%.0f x %.0f", textureWidth, textureHeight);
}

void CSpriteViewerPanel::DrawSheetPane(const CSpriteAsset& spriteAsset,
                                       const std::vector<SpriteFrame>& frames,
                                       const ImVec2& paneSize)
{
	ImGui::BeginChild("##sprite_sheet_pane", paneSize, true, ImGuiWindowFlags_HorizontalScrollbar);

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	CSpriteAsset& mutableAsset = const_cast<CSpriteAsset&>(spriteAsset);
	const ImTextureID textureId = AcquireTextureId(m_guid, mutableAsset);
	const ImVec2 drawSize(static_cast<float>(spriteAsset.GetWidth()) * m_zoom,
	                      static_cast<float>(spriteAsset.GetHeight()) * m_zoom);

	if (0 != textureId)
	{
		ImGui::Image(textureId, drawSize);
	}
	else
	{
		// GPU 텍스처를 못 얻어도 격자는 의미가 있다 — 슬라이스 수치를 확인할 수 있다.
		ImGui::Dummy(drawSize);
	}

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const bool canHover = ImGui::IsWindowHovered();
	const ImVec2 mouse = ImGui::GetMousePos();
	m_hoveredFrame = -1;

	for (std::size_t index = 0; index < frames.size(); ++index)
	{
		const SpriteFrame& frame = frames[index];
		const ImVec2 cellSize(static_cast<float>(frame.Width) * m_zoom,
		                      static_cast<float>(frame.Height) * m_zoom);
		const ImVec2 topLeft(origin.x + static_cast<float>(frame.X) * m_zoom,
		                     origin.y + static_cast<float>(frame.Y) * m_zoom);
		const ImVec2 bottomRight(topLeft.x + cellSize.x, topLeft.y + cellSize.y);

		const bool hovered = canHover
			&& mouse.x >= topLeft.x && mouse.x <= bottomRight.x
			&& mouse.y >= topLeft.y && mouse.y <= bottomRight.y;
		if (hovered)
		{
			m_hoveredFrame = static_cast<int>(index);
			// 칸을 클릭하면 그 프레임을 고른다 — 오른쪽 미리보기가 바로 그걸 보여 준다.
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				m_selectedFrame = static_cast<int>(index);
				m_playing = false;
			}
		}

		const bool selected = (static_cast<int>(index) == m_selectedFrame);
		const ImU32 color = hovered ? HOVER_COLOR : (selected ? SELECTED_COLOR : GRID_COLOR);
		drawList->AddRect(topLeft, bottomRight, color, 0.0f, 0, (hovered || selected) ? 2.0f : 1.0f);

		if (cellSize.x >= LABEL_MIN_CELL_PIXELS && cellSize.y >= LABEL_MIN_CELL_PIXELS)
		{
			char label[16] = {};
			std::snprintf(label, sizeof(label), "%d", static_cast<int>(index));
			drawList->AddText(ImVec2(topLeft.x + 3.0f, topLeft.y + 2.0f), LABEL_COLOR, label);
		}

		if (m_showPivot)
		{
			DrawPivotCross(*drawList, topLeft, cellSize, frame.PivotX, frame.PivotY);
		}
	}

	ImGui::EndChild();
}

void CSpriteViewerPanel::DrawSidePane(const AssetMetaData& metaData,
                                      const CSpriteAsset& spriteAsset,
                                      const std::vector<SpriteFrame>& frames,
                                      const ImVec2& paneSize)
{
	ImGui::SameLine();

	// 겉 껍데기는 스크롤하지 않는다 — 옵션이 길어졌을 때 미리보기까지 같이 밀려 올라가면
	// 정작 보려던 그림이 화면 밖으로 나간다.
	ImGui::BeginChild("##sprite_side_pane", paneSize, false,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	DrawPreview(spriteAsset, frames, paneSize.x);

	// 임포트 옵션 — 인스펙터와 **같은 값**을 편집한다(SpriteImportOptionsEditor 소유).
	// 여기서 고친 게 인스펙터에도 그대로 보이고, 그 반대도 같다.
	// 스크롤은 이 안쪽에만 생긴다.
	ImGui::BeginChild("##sprite_options_scroll", ImVec2(0.0f, 0.0f), true);
	ImSectionHeader(Loc::Text(EditorLocKeys::InspectorSpriteImportOptions)).Draw();
	SpriteImportOptionsEditor::DrawEditor(metaData);
	SpriteImportOptionsEditor::DrawSliceSummary(metaData);
	SpriteImportOptionsEditor::DrawApplyButton(metaData);
	ImGui::EndChild();

	ImGui::EndChild();
}

void CSpriteViewerPanel::DrawPreview(const CSpriteAsset& spriteAsset,
                                     const std::vector<SpriteFrame>& frames,
                                     float paneWidth)
{
	ImGui::TextUnformatted(Loc::Text(EditorLocKeys::SpriteViewerPreview));

	// 미리보기 칸 높이는 고정한다 — 프레임마다 크기가 달라도 아래 옵션이 들썩이지 않게.
	// 스크롤도 막는다. 그림은 항상 칸 안에 맞춰 그리므로 넘칠 일이 없고,
	// 휠은 아래 옵션 목록이 받아야 한다.
	const float previewHeight = std::max(paneWidth * 0.6f, 120.0f);
	ImGui::BeginChild("##sprite_preview", ImVec2(0.0f, previewHeight), true,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	if (frames.empty())
	{
		ImGui::EndChild();
		return;
	}

	const SpriteFrame& frame = frames[static_cast<std::size_t>(m_selectedFrame)];
	const float textureWidth = static_cast<float>(spriteAsset.GetWidth());
	const float textureHeight = static_cast<float>(spriteAsset.GetHeight());
	const float frameWidth = static_cast<float>(frame.Width);
	const float frameHeight = static_cast<float>(frame.Height);

	// 칸 안에 통째로 들어가되 확대는 하지 않는다(픽셀아트가 뭉개지지 않게).
	const ImVec2 boxAvail = ImGui::GetContentRegionAvail();
	float scale = 1.0f;
	if (frameWidth > 0.0f && frameHeight > 0.0f)
	{
		scale = std::min(boxAvail.x / frameWidth, boxAvail.y / frameHeight);
		scale = std::min(scale, 8.0f);
	}
	const ImVec2 drawSize(frameWidth * scale, frameHeight * scale);

	// 가운데 정렬.
	const ImVec2 pad((boxAvail.x - drawSize.x) * 0.5f, (boxAvail.y - drawSize.y) * 0.5f);
	ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + std::max(pad.x, 0.0f),
	                           ImGui::GetCursorPosY() + std::max(pad.y, 0.0f)));

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	CSpriteAsset& mutableAsset = const_cast<CSpriteAsset&>(spriteAsset);
	const ImTextureID textureId = AcquireTextureId(m_guid, mutableAsset);
	if (0 != textureId && textureWidth > 0.0f && textureHeight > 0.0f)
	{
		// 프레임 하나만 잘라 그린다 — 시트 전체를 그린 뒤 가리는 것보다 확대에 유리하다.
		const ImVec2 uv0(static_cast<float>(frame.X) / textureWidth,
		                 static_cast<float>(frame.Y) / textureHeight);
		const ImVec2 uv1(static_cast<float>(frame.X + frame.Width) / textureWidth,
		                 static_cast<float>(frame.Y + frame.Height) / textureHeight);
		ImGui::Image(textureId, drawSize, uv0, uv1);
	}
	else
	{
		ImGui::Dummy(drawSize);
	}

	if (m_showPivot)
	{
		DrawPivotCross(*ImGui::GetWindowDrawList(), origin, drawSize, frame.PivotX, frame.PivotY);
	}

	ImGui::EndChild();
}

void CSpriteViewerPanel::DrawPlaybackBar(const std::vector<SpriteFrame>& frames)
{
	const std::size_t frameCount = frames.size();
	const int lastIndex = frameCount > 0 ? static_cast<int>(frameCount) - 1 : 0;

	ImGui::BeginDisabled(frameCount <= 1);
	if (ImGui::Button(m_playing ? Loc::Text(EditorLocKeys::SpriteViewerStop)
	                            : Loc::Text(EditorLocKeys::SpriteViewerPlay)))
	{
		m_playing = false == m_playing;
		m_elapsedSeconds = 0.0f;
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::SetNextItemWidth(260.0f);
	// 재생 중에는 직접 못 끌게 한다 — 끌자마자 재생이 덮어써서 조작이 안 먹는 것처럼 보인다.
	ImGui::BeginDisabled(m_playing || frameCount == 0);
	ImGui::SliderInt("##sprite_viewer.frame_index", &m_selectedFrame, 0, lastIndex,
		Loc::Text(EditorLocKeys::SpriteViewerFrameIndexFormat));
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::SetNextItemWidth(130.0f);
	ImGui::SliderFloat(Loc::Text(EditorLocKeys::SpriteViewerFramesPerSecond), &m_framesPerSecond, 1.0f, 60.0f, "%.0f");

	// 가리키고 있는 칸이 있으면 그쪽 정보를 보여 준다(격자에서 찾을 때 유용).
	if (m_hoveredFrame >= 0 && m_hoveredFrame < static_cast<int>(frameCount))
	{
		const SpriteFrame& hovered = frames[static_cast<std::size_t>(m_hoveredFrame)];
		ImGui::SameLine();
		ImGui::TextDisabled("%s %d   (%u, %u)  %u x %u",
			Loc::Text(EditorLocKeys::SpriteViewerHoveredFrame), m_hoveredFrame,
			hovered.X, hovered.Y, hovered.Width, hovered.Height);
	}

	AdvancePlayback(frameCount);
}

void CSpriteViewerPanel::AdvancePlayback(std::size_t frameCount)
{
	if (false == m_playing || frameCount <= 1 || m_framesPerSecond <= 0.0f)
	{
		return;
	}

	m_elapsedSeconds += ImGui::GetIO().DeltaTime;
	const float frameDuration = 1.0f / m_framesPerSecond;
	if (m_elapsedSeconds < frameDuration)
	{
		return;
	}

	// while 로 돌리면 창이 오래 멈췄다 돌아왔을 때 델타가 커져 수천 번 돈다. 나눗셈으로 끝낸다.
	const int advance = std::min(static_cast<int>(m_elapsedSeconds / frameDuration), MAX_PLAYBACK_ADVANCE);
	m_elapsedSeconds -= static_cast<float>(advance) * frameDuration;
	m_selectedFrame = (m_selectedFrame + advance) % static_cast<int>(frameCount);
}
