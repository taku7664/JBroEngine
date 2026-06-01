#include "pch.h"
#include "GameViewTool.h"

#include "Editor/Editor.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/GameFramework/Component/Camera2D.h"
#include "Engine/GameFramework/Component/GameObject.h"
#include "Engine/GameFramework/Component/Transform2D.h"
#include "Engine/GameFramework/Scene/Scene.h"
#include "Engine/GameFramework/Scene/SceneTransformUtils.h"

#include <algorithm>
#include <vector>

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

	// ── Camera collection ─────────────────────────────────────────────────────────
	// Collects ALL active Camera2D components in the scene and returns them as
	// GameCameraDesc sorted by Priority (ascending: lowest priority renders first,
	// highest priority renders last / on top).
	// resW/resH: 게임 렌더 타겟(프로젝트) 해상도 — Layout2D 계산에 사용됩니다.
	std::vector<GameCameraDesc> FindAllActiveGameCameras(const CScene& scene, float resW, float resH)
	{
		std::vector<GameCameraDesc> cameras;

		scene.ForEach<GameObject, Transform2D, Camera2D>(
			[&](EntityId entity,
			    const GameObject& go,
			    const Transform2D&,
			    const Camera2D& cam)
			{
				if (false == go.IsActive || false == cam.IsEnabled)
				{
					return;
				}

				const Matrix3x2 wt = GetWorldTransform(scene, entity);

				// Layout2D → 픽셀 위치 / 크기 계산
				const Vector2 posPixel  = cam.Position.Resolve(resW, resH);
				      Vector2 sizePixel = cam.Size.Resolve(resW, resH);
				if (sizePixel.x < 1.0f) sizePixel.x = 1.0f;
				if (sizePixel.y < 1.0f) sizePixel.y = 1.0f;

				// ── 월드 스케일 — X·Y 독립 적용 ────────────────────────────────────
				//   OrthoSize  = baseOrtho * scaleY  → 세로 반높이(halfH)
				//   OrthoSizeX = baseOrtho * scaleX * baseAspect → 가로 반너비(halfW)
				//   SetViewCameraEx 로 전달 → 렌더러가 뷰포트 비율 파생 없이 직접 NDC 매핑.
				//   GPU가 viewport 픽셀 크기로 자연스럽게 스트레칭합니다.
				const float sX = std::sqrt(wt.M11 * wt.M11 + wt.M12 * wt.M12);
				const float sY = std::sqrt(wt.M21 * wt.M21 + wt.M22 * wt.M22);
				const float safeScaleX = std::max(sX, 0.0001f);
				const float safeScaleY = std::max(sY, 0.0001f);
				const float baseOrtho  = cam.OrthographicSize > 0.0f ? cam.OrthographicSize : 5.0f;
				const float baseAspect = resW / resH;

				// 카메라 회전: 월드 트랜스폼에서 cos/sin 추출.
				// wt.M11 = cosθ * scaleX,  wt.M12 = sinθ * scaleX
				const float cosR = sX > 1e-6f ? wt.M11 / sX : 1.0f;
				const float sinR = sX > 1e-6f ? wt.M12 / sX : 0.0f;

				// ImEditor는 뷰포트를 [0,1] 정규화 좌표로 받습니다.
				GameCameraDesc desc;
				desc.PosX        = wt.Dx;
				desc.PosY        = wt.Dy;
				desc.OrthoSize   = baseOrtho * safeScaleY;              // halfH
				desc.OrthoSizeX  = baseOrtho * safeScaleX * baseAspect; // halfW
				desc.CosR        = cosR;
				desc.SinR        = sinR;
				desc.ViewportX   = posPixel.x  / resW;
				desc.ViewportY   = posPixel.y  / resH;
				desc.ViewportW   = sizePixel.x / resW;
				desc.ViewportH   = sizePixel.y / resH;
				desc.ClearColor[0] = cam.ClearColor[0];
				desc.ClearColor[1] = cam.ClearColor[1];
				desc.ClearColor[2] = cam.ClearColor[2];
				desc.ClearColor[3] = cam.ClearColor[3];
				desc.Priority    = cam.Priority;
				desc.IsMainCamera = cam.IsMainCamera;

				cameras.push_back(desc);
			});

		// Sort: lowest priority value renders first (background).
		std::sort(cameras.begin(), cameras.end(),
			[](const GameCameraDesc& a, const GameCameraDesc& b)
			{
				return a.Priority < b.Priority;
			});

		return cameras;
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
		std::vector<GameCameraDesc> cameras;
		if (Core::SceneManager)
		{
			SafePtr<CScene> scene = Core::SceneManager->GetActiveScene();
			if (scene)
			{
				cameras = FindAllActiveGameCameras(*scene, resW, resH);
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
	const bool hasScene  = Core::SceneManager.IsValid() && Core::SceneManager->GetActiveScene().IsValid();
	const bool isPlaying = Core::SceneManager.IsValid() && Core::SceneManager->IsSimulationPlaying();

	const ImVec2 textPos = vpMin + ImVec2(12.0f, 10.0f);
	const ImU32  textCol = isPlaying ? IM_COL32(100, 230, 120, 255) : IM_COL32(210, 216, 224, 255);
	const char* statusText = isPlaying
		? Loc::Text("game_view.status.playing")
		: (hasScene ? Loc::Text("game_view.status.scene_stopped") : Loc::Text("game_view.status.no_active_scene"));
	dl->AddText(textPos, textCol, statusText);

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
