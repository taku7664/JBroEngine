#include "pch.h"
#include "SpriteViewerWindow.h"

#include "Editor/Editor.h"
#include "Editor/EditorContext.h"
#include "Editor/Localization/EditorLocalizationKeys.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Core/Renderer/IRenderResourceCache.h"
#include "Engine/Core/RHI/IRHITexture.h"
#include "Engine/Editor/ImEditor.h"
#include "ThirdParty/imgui/imgui.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace
{
	constexpr float ZOOM_MIN = 0.05f;
	constexpr float ZOOM_MAX = 16.0f;

	// 셀이 이보다 작으면 인덱스 라벨을 그리지 않는다 — 겹쳐서 격자만 더럽힌다.
	constexpr float LABEL_MIN_CELL_PIXELS = 22.0f;

	// 한 프레임에 애니메이션이 건너뛸 수 있는 최대 프레임 수. 창을 오래 멈춰 뒀다가
	// 돌아왔을 때(브레이크포인트·드래그) 델타가 커져도 루프가 폭주하지 않게 막는다.
	constexpr int MAX_PLAYBACK_ADVANCE = 240;

	const ImU32 GRID_COLOR     = IM_COL32(80, 200, 255, 180);
	const ImU32 HOVER_COLOR    = IM_COL32(255, 220, 90, 255);
	const ImU32 SELECTED_COLOR = IM_COL32(120, 255, 140, 255);
	const ImU32 PIVOT_COLOR    = IM_COL32(255, 110, 110, 230);
	const ImU32 LABEL_COLOR    = IM_COL32(255, 255, 255, 200);

	std::string DockKey(const AssetGuid& guid)  { return std::string("SpriteViewerDock_")  + guid.generic_string(); }
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

	// 프레임 하나를 감싸는 피벗 십자. 피벗은 **Y-up**(0=아래, 1=위)이고 텍스처 좌표는
	// Y-down 이라 세로를 뒤집어 찍는다.
	void DrawPivotCross(ImDrawList& drawList, const ImVec2& topLeft, const ImVec2& size,
	                    float pivotX, float pivotY)
	{
		const float x = topLeft.x + pivotX * size.x;
		const float y = topLeft.y + (1.0f - pivotY) * size.y;
		drawList.AddLine(ImVec2(x - 5.0f, y), ImVec2(x + 5.0f, y), PIVOT_COLOR, 1.5f);
		drawList.AddLine(ImVec2(x, y - 5.0f), ImVec2(x, y + 5.0f), PIVOT_COLOR, 1.5f);
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

		const ImGuiID dockId  = ImHashStr(DockKey(guid).c_str());
		const ImGuiID panelId = ImHashStr(PanelKey(guid).c_str());

		// 이미 열려 있으면 새로 만들지 않고 다시 보여 준다.
		if (SafePtr<CSpriteViewerPanel> existing =
			DynamicSafePtrCast<CSpriteViewerPanel>(Editor::ImEditor->FindImWindow(panelId)))
		{
			if (SafePtr<CImWindow> existingDock = DynamicSafePtrCast<CImWindow>(Editor::ImEditor->FindImWindow(dockId)))
			{
				existingDock->SetVisible(true);
			}
			existing->SetVisible(true);
			existing->Focus();
			return;
		}

		SafePtr<CSpriteViewerDockWindow> dock =
			Editor::ImEditor->CreateImWindow<CSpriteViewerDockWindow>(DockKey(guid).c_str(), Editor::RootDockWindow->GetID());
		if (false == dock.IsValid())
		{
			if (SafePtr<CImWindow> d = DynamicSafePtrCast<CImWindow>(Editor::ImEditor->FindImWindow(dockId)))
			{
				d->Focus();
			}
			return;
		}
		// Dock 제목은 파일명이 아니라 "스프라이트 뷰어" 로 고정한다(언어 전환도 반영).
		// 파일명은 안쪽 패널 탭이 들고 있다 — 사운드 효과 에디터와 같은 규칙.
		dock->SetLocalizedTitleKey(EditorLocKeys::SpriteViewerTitle);
		dock->SetSize(ImVec2(760.0f, 620.0f));

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

void SpriteViewer::PushOptions(const AssetGuid& guid, const SpriteImportOptions& options)
{
	if (false == Editor::ImEditor.IsValid() || guid.IsNull())
	{
		return;
	}
	const ImGuiID panelId = ImHashStr(PanelKey(guid).c_str());
	if (SafePtr<CSpriteViewerPanel> panel =
		DynamicSafePtrCast<CSpriteViewerPanel>(Editor::ImEditor->FindImWindow(panelId)))
	{
		panel->SetOptions(options);
	}
}

void CSpriteViewerPanel::OnRenderStay()
{
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

	// 격자는 **편집 중인 옵션**으로 다시 계산한다. 자산이 들고 있는 프레임은 마지막으로
	// Apply 된 결과라, 슬라이더를 움직이는 동안에는 화면과 어긋난다.
	const std::vector<SpriteFrame> frames =
		CSpriteImportOptions::BuildFrames(spriteAsset->GetWidth(), spriteAsset->GetHeight(), m_options);

	// 슬라이스를 다시 잘라 프레임 수가 줄면 선택/재생 인덱스가 범위를 벗어난다.
	if (false == frames.empty())
	{
		m_selectedFrame = std::clamp(m_selectedFrame, 0, static_cast<int>(frames.size()) - 1);
	}
	else
	{
		m_selectedFrame = 0;
	}

	DrawToolbar(static_cast<float>(spriteAsset->GetWidth()), static_cast<float>(spriteAsset->GetHeight()));
	if (ESpriteViewerMode::Frame == m_mode)
	{
		DrawFrameControls(frames.size());
	}
	ImGui::Separator();

	// 캔버스를 먼저 그린다 — 호버 판정이 여기서 나오므로 상태줄은 그 **뒤**여야 한다.
	// 반대로 두면 표시가 한 프레임 늦어 커서를 못 따라온다.
	if (ESpriteViewerMode::Sheet == m_mode)
	{
		DrawSheet(*spriteAsset, frames);
	}
	else
	{
		DrawSingleFrame(*spriteAsset, frames);
	}

	DrawStatusLine(frames);
}

void CSpriteViewerPanel::DrawToolbar(float textureWidth, float textureHeight)
{
	const bool sheetMode = (ESpriteViewerMode::Sheet == m_mode);
	if (ImGui::RadioButton(Loc::Text(EditorLocKeys::SpriteViewerModeSheet), sheetMode))
	{
		m_mode = ESpriteViewerMode::Sheet;
		m_playing = false;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton(Loc::Text(EditorLocKeys::SpriteViewerModeFrame), false == sheetMode))
	{
		m_mode = ESpriteViewerMode::Frame;
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(160.0f);
	ImGui::SliderFloat(Loc::Text(EditorLocKeys::SpriteViewerZoom), &m_zoom, ZOOM_MIN, ZOOM_MAX, "%.2fx");

	ImGui::SameLine();
	if (ImGui::Button(Loc::Text(EditorLocKeys::SpriteViewerFit)))
	{
		// 캔버스에 남을 높이 = 지금 남은 높이 - (아래에 더 그릴 줄들). 폰트/DPI 가 바뀌어도
		// 따라가도록 상수 대신 ImGui 의 실제 줄 높이를 쓴다.
		const float reservedRows = (ESpriteViewerMode::Frame == m_mode) ? 3.0f : 2.0f;
		const float reserved = ImGui::GetFrameHeightWithSpacing() * reservedRows;
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const float fitX = textureWidth > 0.0f ? available.x / textureWidth : 1.0f;
		const float fitY = textureHeight > 0.0f ? (available.y - reserved) / textureHeight : 1.0f;
		m_zoom = std::clamp(std::min(fitX, fitY), ZOOM_MIN, ZOOM_MAX);
	}

	ImGui::SameLine();
	ImGui::Checkbox(Loc::Text(EditorLocKeys::SpriteViewerShowPivot), &m_showPivot);

	ImGui::SameLine();
	ImGui::TextDisabled("%.0f x %.0f", textureWidth, textureHeight);
}

void CSpriteViewerPanel::DrawFrameControls(std::size_t frameCount)
{
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
	ImGui::SetNextItemWidth(200.0f);
	// 재생 중에는 인덱스를 직접 못 끌게 한다 — 끌자마자 재생이 덮어써서 조작이 안 먹는 것처럼 보인다.
	ImGui::BeginDisabled(m_playing);
	ImGui::SliderInt("##sprite_viewer.frame_index", &m_selectedFrame, 0, lastIndex,
		Loc::Text(EditorLocKeys::SpriteViewerFrameIndexFormat));
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::SetNextItemWidth(140.0f);
	ImGui::SliderFloat(Loc::Text(EditorLocKeys::SpriteViewerFramesPerSecond), &m_framesPerSecond, 1.0f, 60.0f, "%.0f");

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

	// while 로 돌리면 창이 오래 멈췄다 돌아왔을 때 델타가 커져 수천 번 돈다. 나눗셈 한 번으로 끝낸다.
	const int advance = std::min(static_cast<int>(m_elapsedSeconds / frameDuration), MAX_PLAYBACK_ADVANCE);
	m_elapsedSeconds -= static_cast<float>(advance) * frameDuration;
	m_selectedFrame = (m_selectedFrame + advance) % static_cast<int>(frameCount);
}

void CSpriteViewerPanel::DrawSheet(const CSpriteAsset& spriteAsset, const std::vector<SpriteFrame>& frames)
{
	const float textureWidth = static_cast<float>(spriteAsset.GetWidth());
	const float textureHeight = static_cast<float>(spriteAsset.GetHeight());

	ImGui::BeginChild("##sprite_sheet_canvas", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), false,
		ImGuiWindowFlags_HorizontalScrollbar);

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	CSpriteAsset& mutableAsset = const_cast<CSpriteAsset&>(spriteAsset);
	const ImTextureID textureId = AcquireTextureId(m_guid, mutableAsset);
	const ImVec2 drawSize(textureWidth * m_zoom, textureHeight * m_zoom);

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
			// 칸을 클릭하면 그 프레임을 고른다 — 프레임 모드로 넘어가면 바로 그게 보인다.
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				m_selectedFrame = static_cast<int>(index);
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

void CSpriteViewerPanel::DrawSingleFrame(const CSpriteAsset& spriteAsset, const std::vector<SpriteFrame>& frames)
{
	ImGui::BeginChild("##sprite_frame_canvas", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), false,
		ImGuiWindowFlags_HorizontalScrollbar);

	m_hoveredFrame = -1;
	if (frames.empty())
	{
		ImGui::EndChild();
		return;
	}

	const SpriteFrame& frame = frames[static_cast<std::size_t>(m_selectedFrame)];
	const float textureWidth = static_cast<float>(spriteAsset.GetWidth());
	const float textureHeight = static_cast<float>(spriteAsset.GetHeight());

	CSpriteAsset& mutableAsset = const_cast<CSpriteAsset&>(spriteAsset);
	const ImTextureID textureId = AcquireTextureId(m_guid, mutableAsset);
	const ImVec2 drawSize(static_cast<float>(frame.Width) * m_zoom,
	                      static_cast<float>(frame.Height) * m_zoom);
	const ImVec2 origin = ImGui::GetCursorScreenPos();

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

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRect(origin, ImVec2(origin.x + drawSize.x, origin.y + drawSize.y), GRID_COLOR);
	if (m_showPivot)
	{
		DrawPivotCross(*drawList, origin, drawSize, frame.PivotX, frame.PivotY);
	}

	ImGui::EndChild();
}

void CSpriteViewerPanel::DrawStatusLine(const std::vector<SpriteFrame>& frames)
{
	if (frames.empty())
	{
		ImGui::TextDisabled("%s 0", Loc::Text(EditorLocKeys::SpriteViewerFrameCount));
		return;
	}

	// 호버가 있으면 그쪽을, 없으면 선택된 프레임을 설명한다.
	const int described = (m_hoveredFrame >= 0 && m_hoveredFrame < static_cast<int>(frames.size()))
		? m_hoveredFrame
		: m_selectedFrame;
	const SpriteFrame& frame = frames[static_cast<std::size_t>(described)];

	ImGui::Text("%s %d / %d      %s %d      (%u, %u)  %u x %u",
		Loc::Text(EditorLocKeys::SpriteViewerFrameCount), described, static_cast<int>(frames.size()) - 1,
		(m_hoveredFrame >= 0) ? Loc::Text(EditorLocKeys::SpriteViewerHoveredFrame)
		                      : Loc::Text(EditorLocKeys::SpriteViewerSelectedFrame),
		described, frame.X, frame.Y, frame.Width, frame.Height);
}
