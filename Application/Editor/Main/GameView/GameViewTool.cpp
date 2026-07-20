#include "pch.h"
#include "GameViewTool.h"

#include "Editor/Editor.h"
#include "Editor/EditorContext.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/GameFramework/Rendering/GameCamera.h"
#include "Engine/GameFramework/Canvas/Canvas.h"
#include "Core/EngineCore.h"
#include "Core/Input/InputSystem.h"

namespace
{
	// 현재 프로젝트 해상도를 반환합니다. 프로젝트가 없으면 fallback 값을 사용합니다.
	void GetProjectResolution(float fallbackW, float fallbackH, float& outW, float& outH)
	{
		SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
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

}

// ── CGameViewTool ──────────────────────────────────────────────────────────────

void CGameViewTool::OnCreate()
{
	SetLocalizedTitleKey(EditorLocKeys::WindowGameView);

	// 에디터 baseline — GameView 가 포커스되기 전엔 게임 입력 비활성(다른 패널 편집 중 누출 방지).
	// 스탠드얼론 게임에는 GameViewTool 이 없어 InputSystem 기본 true 가 유지된다.
	if (Engine.InputSystem.IsValid())
	{
		Engine.InputSystem->SetViewportActive(false);
	}
}

void CGameViewTool::OnDestroy()
{
	// 패널 파괴 시 게이트 해제 — 잔여 활성 상태로 입력이 새지 않도록.
	if (Engine.InputSystem.IsValid())
	{
		Engine.InputSystem->SetViewportActive(false);
	}
}

void CGameViewTool::OnFocusEnter()
{
	if (Engine.InputSystem.IsValid())
	{
		Engine.InputSystem->SetViewportActive(true);
	}
}

void CGameViewTool::OnFocusExit()
{
	if (Engine.InputSystem.IsValid())
	{
		Engine.InputSystem->SetViewportActive(false);
	}
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

	// 캔버스 핸들만 ImEditor 에 넘긴다 — 카메라/라이트 스냅샷은 ImEditor 의
	// PrepareRender(시뮬 이후·렌더 직전)가 캔버스에서 직접 수집한다. 여기(UI 빌드,
	// 캔버스 업데이트 전) 수집분은 "활성 카메라 존재" 판정(RT 요청/해제)에만 쓴다.
	if (Editor::ImEditor)
	{
		std::vector<GameRenderViewportDesc> viewports;
		SafePtr<CGameCanvas> canvas;
		if (Engine.CanvasManager)
		{
			canvas = EditorContext::GetActiveCanvas();
			if (canvas)
			{
				// 뷰포트 수집은 카메라 Ref 를 해석한다 — 눈이 하나도 없으면 빈 목록이 되므로
				// "그릴 게 있는가" 판정이 그대로 성립한다.
				viewports = CollectGameRenderViewports(*canvas, resW, resH);
			}
		}

		if (false == viewports.empty())
		{
			Editor::ImEditor->SetGameViewCanvas(canvas);
			// RT는 프로젝트 해상도로 요청합니다.
			Editor::ImEditor->RequestGameViewRenderTarget(
				static_cast<std::uint32_t>(resW),
				static_cast<std::uint32_t>(resH));
		}
		else
		{
			// No active Camera2D — release the RT so texID becomes null.
			Editor::ImEditor->SetGameViewCanvas(nullptr);
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

		// 게임 입력이 쓸 마우스 좌표계를 이 그림 렉트에 맞춘다 — 스크립트가 보는 마우스는
		// 창이 아니라 **게임 화면** 기준이어야 Canvas::ScreenToWorld 와 좌표계가 맞는다.
		//
		// 멀티뷰포트(ImGuiConfigFlags_ViewportsEnable)가 켜져 있어 ImGui 의 "스크린 좌표"는
		// 데스크톱 좌표다. 반면 InputSystem 은 메인 창의 **클라이언트** 좌표로 마우스를 폴링한다
		// (ScreenToClient). 그대로 넘기면 창 위치만큼 어긋난다 — 뷰포트 원점을 빼서 맞춘다.
		// 게임뷰를 메인 창 밖으로 떼어내면(별도 OS 창) 마우스 폴링 기준 창이 달라져 여전히
		// 어긋난다 — 그건 별개 한계다(도킹 상태가 정상 사용).
		if (Engine.InputSystem)
		{
			const ImGuiViewport* windowViewport = ImGui::GetWindowViewport();
			const ImVec2 viewportOrigin = windowViewport ? windowViewport->Pos : ImVec2(0.0f, 0.0f);
			Engine.InputSystem->SetGameSurfaceRect(
				drawX - viewportOrigin.x, drawY - viewportOrigin.y, drawW, drawH, resW, resH);
		}
	}
	else
	{
		dl->AddRect(vpMin, vpMax, IM_COL32(60, 66, 76, 255));
	}

	// ── Status overlay ─────────────────────────────────────────────────────────
	const bool hasCanvas  = EditorContext::GetActiveCanvas().IsValid();
	const bool isPlaying = Engine.CanvasManager.IsValid() && Engine.CanvasManager->IsSimulationPlaying();

	const ImVec2 textPos = vpMin + ImVec2(12.0f, 10.0f);
	const ImU32  textCol = isPlaying ? IM_COL32(100, 230, 120, 255) : IM_COL32(210, 216, 224, 255);
	const char* statusText = isPlaying
		? Loc::Text(EditorLocKeys::GameViewStatusPlaying)
		: (hasCanvas ? Loc::Text(EditorLocKeys::GameViewStatusCanvasStopped) : Loc::Text(EditorLocKeys::GameViewStatusNoActiveCanvas));
	dl->AddText(textPos, textCol, statusText);

	if (nullptr == texID)
	{
		// Centered "No Camera" notice.
		const char* msg = Loc::Text(EditorLocKeys::GameViewNoCamera);
		const ImVec2 center = ImVec2(
			vpMin.x + vpSize.x * 0.5f - ImGui::CalcTextSize(msg).x * 0.5f,
			vpMin.y + vpSize.y * 0.5f - 8.0f);
		dl->AddText(center, IM_COL32(130, 130, 140, 200), msg);
	}

	dl->PopClipRect();
}
