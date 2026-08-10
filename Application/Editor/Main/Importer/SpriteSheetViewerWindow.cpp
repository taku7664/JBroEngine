#include "pch.h"
#include "SpriteSheetViewerWindow.h"

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
	constexpr float ZOOM_MIN = 0.1f;
	constexpr float ZOOM_MAX = 16.0f;

	// 셀이 이보다 작으면 인덱스 라벨을 그리지 않는다 — 겹쳐서 격자만 더럽힌다.
	constexpr float LABEL_MIN_CELL_PIXELS = 22.0f;

	std::string ViewerKey(const AssetGuid& guid)
	{
		return std::string("SpriteSheetViewer_") + guid.generic_string();
	}

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
}

void SpriteSheetViewer::Open(const AssetGuid& guid, const std::string& title)
{
	if (false == Editor::ImEditor.IsValid() || guid.IsNull())
	{
		return;
	}

	// 버튼 클릭은 윈도우 draw/순회 도중 발생한다. 여기서 바로 CreateImWindow 하면
	// 윈도우 벡터가 재할당되어 순회 중 반복자가 무효화된다 → 다음 Update 끝으로 지연.
	Editor::ImEditor->QueueDeferred([guid, title]()
	{
		if (false == Editor::ImEditor.IsValid())
		{
			return;
		}

		const std::string key = ViewerKey(guid);
		const ImGuiID id = ImHashStr(key.c_str());

		if (SafePtr<CSpriteSheetViewerWindow> existing =
			DynamicSafePtrCast<CSpriteSheetViewerWindow>(Editor::ImEditor->FindImWindow(id)))
		{
			existing->SetVisible(true);
			existing->Focus();
			return;
		}

		// 부모 0 = 독에 붙지 않는 독립 창. 시트를 크게 보려면 떼어 놓는 편이 낫다.
		SafePtr<CSpriteSheetViewerWindow> viewer =
			Editor::ImEditor->CreateImWindow<CSpriteSheetViewerWindow>(key.c_str(), 0);
		if (false == viewer.IsValid())
		{
			return;
		}
		viewer->SetTitle(title.c_str());
		viewer->SetTargetGuid(guid);
		viewer->SetSize(ImVec2(720.0f, 560.0f));
		viewer->Focus();
	});
}

void SpriteSheetViewer::PushOptions(const AssetGuid& guid, const SpriteImportOptions& options)
{
	if (false == Editor::ImEditor.IsValid() || guid.IsNull())
	{
		return;
	}
	const ImGuiID id = ImHashStr(ViewerKey(guid).c_str());
	if (SafePtr<CSpriteSheetViewerWindow> viewer =
		DynamicSafePtrCast<CSpriteSheetViewerWindow>(Editor::ImEditor->FindImWindow(id)))
	{
		viewer->SetOptions(options);
	}
}

void CSpriteSheetViewerWindow::OnRenderStay()
{
	SafePtr<IAssetManager> assetManager = EditorContext::GetAssetManager();
	CSpriteAsset* spriteAsset = nullptr;
	if (assetManager)
	{
		// 창이 열려 있는 동안만 붙잡는다 — 닫히면 소멸자가 내린다(자산 캐시에 자동 GC 없음).
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
		ImGui::TextUnformatted(Loc::Text(EditorLocKeys::SpriteSheetViewerNoTexture));
		return;
	}

	DrawToolbar(static_cast<float>(spriteAsset->GetWidth()), static_cast<float>(spriteAsset->GetHeight()));
	ImGui::Separator();
	DrawSheet(*spriteAsset);
}

void CSpriteSheetViewerWindow::DrawToolbar(float textureWidth, float textureHeight)
{
	ImGui::SetNextItemWidth(180.0f);
	ImGui::SliderFloat(Loc::Text(EditorLocKeys::SpriteSheetViewerZoom), &m_zoom, ZOOM_MIN, ZOOM_MAX, "%.2fx");

	ImGui::SameLine();
	if (ImGui::Button(Loc::Text(EditorLocKeys::SpriteSheetViewerFit)))
	{
		// 남은 영역에 통째로 들어가는 배율. 세로/가로 중 빡빡한 쪽에 맞춘다.
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const float fitX = textureWidth > 0.0f ? available.x / textureWidth : 1.0f;
		const float fitY = textureHeight > 0.0f ? (available.y - 40.0f) / textureHeight : 1.0f;
		m_zoom = std::clamp(std::min(fitX, fitY), ZOOM_MIN, ZOOM_MAX);
	}

	ImGui::SameLine();
	ImGui::Checkbox(Loc::Text(EditorLocKeys::SpriteSheetViewerShowPivot), &m_showPivot);

	ImGui::SameLine();
	ImGui::TextDisabled("%.0f x %.0f", textureWidth, textureHeight);
}

void CSpriteSheetViewerWindow::DrawSheet(const CSpriteAsset& spriteAsset)
{
	const float textureWidth = static_cast<float>(spriteAsset.GetWidth());
	const float textureHeight = static_cast<float>(spriteAsset.GetHeight());

	// 격자는 **편집 중인 옵션**으로 다시 계산한다. 자산이 들고 있는 프레임은 마지막으로
	// Apply 된 결과라, 슬라이더를 움직이는 동안에는 화면과 어긋난다.
	const std::vector<SpriteFrame> frames =
		CSpriteImportOptions::BuildFrames(spriteAsset.GetWidth(), spriteAsset.GetHeight(), m_options);

	// 현재 프레임 정보 줄 — 이미지 위에 둬야 스크롤해도 안 사라진다.
	if (m_hoveredFrame >= 0 && m_hoveredFrame < static_cast<int>(frames.size()))
	{
		const SpriteFrame& frame = frames[static_cast<std::size_t>(m_hoveredFrame)];
		ImGui::Text("%s %d   (%u, %u)  %u x %u",
			Loc::Text(EditorLocKeys::SpriteSheetViewerHoveredFrame),
			m_hoveredFrame, frame.X, frame.Y, frame.Width, frame.Height);
	}
	else
	{
		ImGui::Text("%s %d", Loc::Text(EditorLocKeys::SpriteSheetViewerFrameCount),
			static_cast<int>(frames.size()));
	}

	ImGui::BeginChild("##sprite_sheet_canvas", ImVec2(0.0f, 0.0f), false,
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
	const ImU32 gridColor = IM_COL32(80, 200, 255, 180);
	const ImU32 hoverColor = IM_COL32(255, 220, 90, 255);
	const ImU32 pivotColor = IM_COL32(255, 110, 110, 230);
	const ImU32 labelColor = IM_COL32(255, 255, 255, 200);

	const bool canHover = ImGui::IsWindowHovered();
	const ImVec2 mouse = ImGui::GetMousePos();
	m_hoveredFrame = -1;

	for (std::size_t index = 0; index < frames.size(); ++index)
	{
		const SpriteFrame& frame = frames[index];
		const ImVec2 topLeft(origin.x + static_cast<float>(frame.X) * m_zoom,
		                     origin.y + static_cast<float>(frame.Y) * m_zoom);
		const ImVec2 bottomRight(topLeft.x + static_cast<float>(frame.Width) * m_zoom,
		                         topLeft.y + static_cast<float>(frame.Height) * m_zoom);

		const bool hovered = canHover
			&& mouse.x >= topLeft.x && mouse.x <= bottomRight.x
			&& mouse.y >= topLeft.y && mouse.y <= bottomRight.y;
		if (hovered)
		{
			m_hoveredFrame = static_cast<int>(index);
		}

		drawList->AddRect(topLeft, bottomRight, hovered ? hoverColor : gridColor, 0.0f, 0, hovered ? 2.0f : 1.0f);

		if (static_cast<float>(frame.Width) * m_zoom >= LABEL_MIN_CELL_PIXELS
			&& static_cast<float>(frame.Height) * m_zoom >= LABEL_MIN_CELL_PIXELS)
		{
			char label[16] = {};
			std::snprintf(label, sizeof(label), "%d", static_cast<int>(index));
			drawList->AddText(ImVec2(topLeft.x + 3.0f, topLeft.y + 2.0f), labelColor, label);
		}

		if (m_showPivot)
		{
			// 피벗은 **Y-up** 규약(0=아래, 1=위)이고 텍스처 좌표는 Y-down 이라 뒤집는다.
			const float pivotX = topLeft.x + frame.PivotX * static_cast<float>(frame.Width) * m_zoom;
			const float pivotY = topLeft.y + (1.0f - frame.PivotY) * static_cast<float>(frame.Height) * m_zoom;
			drawList->AddLine(ImVec2(pivotX - 4.0f, pivotY), ImVec2(pivotX + 4.0f, pivotY), pivotColor, 1.5f);
			drawList->AddLine(ImVec2(pivotX, pivotY - 4.0f), ImVec2(pivotX, pivotY + 4.0f), pivotColor, 1.5f);
		}
	}

	ImGui::EndChild();
}
