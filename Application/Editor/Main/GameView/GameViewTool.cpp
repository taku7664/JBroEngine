#include "pch.h"
#include "GameViewTool.h"

#include "Editor/Editor.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/GameFramework/Rendering/GameCamera.h"
#include "Engine/GameFramework/Scene/Scene.h"

namespace
{
	// 현재 프로젝트 해상도를 반환합니다. 프로젝트가 없으면 fallback 값을 사용합니다.
	void GetProjectResolution(float fallbackW, float fallbackH, float& outW, float& outH)
	{
		SafePtr<CProjectManager> pm = Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
		if (pm && pm->IsProjectLoaded())
		{
			outW = static_cast<float>(pm->GetResolutionWidth());
			outH = static_cast<float>(pm->GetResolutionHeight());
		}
		else
		{
			outW = fallbackW;
			outH = fallbackH;
		}
	}

	bool IsProjectDebugModeEnabled()
	{
		SafePtr<CProjectManager> pm = Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
		return pm && pm->IsProjectLoaded() && pm->IsDebugModeEnabled();
	}

}

// ── CGameViewTool ──────────────────────────────────────────────────────────────

void CGameViewTool::OnCreate()
{
	SetLocalizedTitleKey("window.game_view");
}

void CGameViewTool::OnDestroy()
{
}

void CGameViewTool::OnUpdate()
{
}

void CGameViewTool::OnRenderStay()
{
	ImVec2 vpSize = ImGui::GetContentRegionAvail();
	vpSize.x = std::max(vpSize.x, 1.0f);
	vpSize.y = std::max(vpSize.y, 1.0f);

	// ── 프로젝트 해상도 가져오기 ────────────────────────────────────────────────
	// RT는 항상 프로젝트 해상도로 생성합니다.
	float resW = vpSize.x;
	float resH = vpSize.y;
	GetProjectResolution(vpSize.x, vpSize.y, resW, resH);

	// Collect all active game cameras and submit them to ImEditor.
	if (Editor::ImEditor)
	{
		std::vector<GameRenderCameraDesc> cameras;
		if (Engine.SceneManager)
		{
			SafePtr<CScene> scene = Engine.SceneManager->GetActiveScene();
			if (scene)
			{
				cameras = CollectGameRenderCameras(*scene, resW, resH);
			}
		}

		if (false == cameras.empty())
		{
			Editor::ImEditor->SetGameViewCameras(cameras);
			// RT는 프로젝트 해상도로 요청합니다.
			Editor::ImEditor->RequestGameViewRenderTarget(
				static_cast<std::uint32_t>(resW),
				static_cast<std::uint32_t>(resH));
		}
		else
		{
			// No active Camera2D — release the RT so texID becomes null.
			Editor::ImEditor->SetGameViewCameras({});
			Editor::ImEditor->RequestGameViewRenderTarget(0, 0);
		}
	}

	const ImVec2 vpMin = ImGui::GetCursorScreenPos();
	const ImVec2 vpMax = vpMin + vpSize;
	ImDrawList*  dl    = ImGui::GetWindowDrawList();

	// InvisibleButton for consistent item sizing (no input needed on game view).
	ImGui::InvisibleButton("##GameViewInput", vpSize);

	// Clip all drawing to the GameView panel bounds.
	dl->PushClipRect(vpMin, vpMax, true);

	// 패널 배경
	dl->AddRectFilled(vpMin, vpMax, IM_COL32(20, 20, 24, 255));

	void* texID = Editor::ImEditor ? Editor::ImEditor->GetGameViewTextureID() : nullptr;
	if (texID)
	{
		// ── 레터박스 (Letter-box / Pillar-box) ─────────────────────────────────
		// RT와 패널 비율이 다를 경우 비율을 유지하면서 패널에 맞춥니다.
		const float rtAspect    = resW / resH;
		const float panelAspect = vpSize.x / vpSize.y;

		float drawW, drawH, drawX, drawY;
		if (rtAspect > panelAspect)
		{
			// RT가 더 넓음 → 너비 맞춤, 위아래 레터박스
			drawW = vpSize.x;
			drawH = vpSize.x / rtAspect;
			drawX = vpMin.x;
			drawY = vpMin.y + (vpSize.y - drawH) * 0.5f;
		}
		else
		{
			// RT가 더 높음 → 높이 맞춤, 좌우 필러박스
			drawH = vpSize.y;
			drawW = vpSize.y * rtAspect;
			drawX = vpMin.x + (vpSize.x - drawW) * 0.5f;
			drawY = vpMin.y;
		}

		dl->AddImage(
			reinterpret_cast<ImTextureID>(texID),
			ImVec2(drawX, drawY),
			ImVec2(drawX + drawW, drawY + drawH),
			ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
	}
	else
	{
		dl->AddRect(vpMin, vpMax, IM_COL32(60, 66, 76, 255));
	}

	// ── Status overlay ─────────────────────────────────────────────────────────
	const bool hasScene  = Engine.SceneManager.IsValid() && Engine.SceneManager->GetActiveScene().IsValid();
	const bool isPlaying = Engine.SceneManager.IsValid() && Engine.SceneManager->IsSimulationPlaying();

	const ImVec2 textPos = vpMin + ImVec2(12.0f, 10.0f);
	const ImU32  textCol = isPlaying ? IM_COL32(100, 230, 120, 255) : IM_COL32(210, 216, 224, 255);
	const char* statusText = isPlaying
		? Loc::Text("game_view.status.playing")
		: (hasScene ? Loc::Text("game_view.status.scene_stopped") : Loc::Text("game_view.status.no_active_scene"));
	dl->AddText(textPos, textCol, statusText);

	if (IsProjectDebugModeEnabled() && Editor::ImEditor)
	{
		const RenderCullingStats stats = Editor::ImEditor->GetGameViewCullingStats();
		char cullingText[128] = {};
		std::snprintf(cullingText, sizeof(cullingText),
			Loc::Text("debug_overlay.culling_format"),
			stats.CulledCount, stats.SubmittedCount, stats.DrawnCount);
		dl->AddText(textPos + ImVec2(0.0f, 20.0f), IM_COL32(255, 205, 90, 230), cullingText);
	}

	if (nullptr == texID)
	{
		// Centered "No Camera" notice.
		const char* msg = Loc::Text("game_view.no_camera");
		const ImVec2 center = ImVec2(
			vpMin.x + vpSize.x * 0.5f - ImGui::CalcTextSize(msg).x * 0.5f,
			vpMin.y + vpSize.y * 0.5f - 8.0f);
		dl->AddText(center, IM_COL32(130, 130, 140, 200), msg);
	}

	dl->PopClipRect();
}
