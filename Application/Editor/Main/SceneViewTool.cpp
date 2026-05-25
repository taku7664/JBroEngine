#include "pch.h"
#include "SceneViewTool.h"

#include "Editor/Editor.h"
#include "Engine/Core/Core.h"
#include "Engine/Core/Debug/DebugDraw.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/GameFramework/Component/Camera2D.h"
#include "Engine/GameFramework/Component/GameObject.h"
#include "Engine/GameFramework/Component/SpriteRenderer2D.h"
#include "Engine/GameFramework/Component/Transform2D.h"
#include "Engine/GameFramework/Debug/SceneDebugDrawSystem.h"
#include "Engine/GameFramework/Scene/Scene.h"
#include "Engine/GameFramework/Scene/SceneTransformUtils.h"

namespace
{
	constexpr int   MAX_GRID_LINES      = 300;
	constexpr float AXIS_LINE_THICKNESS = 2.5f;

	// ── Camera smooth speed ───────────────────────────────────────────────────────
	// Higher  = snappier (reaches target faster)
	// Lower   = more fluid/floaty
	// Typical range: 4 (sluggish) ~ 20 (near-instant)
	// Adjust this value in SceneViewTool.cpp to tune the feel.
	constexpr float CAMERA_SMOOTH_SPEED = 10.0f;

	// ── Coordinate utilities (camera-aware) ───────────────────────────────────────

	float GetAspect(const ImVec2& vpSize)
	{
		return vpSize.y > 0.0f ? vpSize.x / vpSize.y : 1.0f;
	}

	Vector2<float> ViewportToWorld(
		const ImVec2& vpPt,
		const ImVec2& vpMin, const ImVec2& vpSize,
		const Vector2<float>& camPos, float camSize)
	{
		const float ndcX = ((vpPt.x - vpMin.x) / vpSize.x) * 2.0f - 1.0f;
		const float ndcY = 1.0f - ((vpPt.y - vpMin.y) / vpSize.y) * 2.0f;
		const float aspect = GetAspect(vpSize);
		return Vector2<float>(
			ndcX * camSize * aspect + camPos.x,
			ndcY * camSize          + camPos.y);
	}

	// ── Grid submission to DebugDraw ──────────────────────────────────────────────
	// World-space grid lines are submitted to IDebugDraw every frame.
	// CImEditor::OnPrepareRender() renders them GPU-side into the scene RT.

	void SubmitGrid(
		IDebugDraw& debugDraw,
		float camX, float camY, float camSize, float aspect)
	{
		const float halfW  = camSize * aspect;
		const float worldL = camX - halfW;
		const float worldR = camX + halfW;
		const float worldB = camY - camSize;
		const float worldT = camY + camSize;

		// Adaptive step: target ~10 lines per axis.
		const float rawStep   = (2.0f * camSize) / 10.0f;
		const float magnitude = std::powf(10.0f,
			std::floorf(std::log10f(std::max(rawStep, 1e-6f))));
		const float normalized = rawStep / magnitude;
		float step;
		if      (normalized < 1.5f) step = 1.0f  * magnitude;
		else if (normalized < 3.5f) step = 2.0f  * magnitude;
		else if (normalized < 7.5f) step = 5.0f  * magnitude;
		else                        step = 10.0f * magnitude;

		constexpr DebugColor gridCol  = DebugColorRGBA( 80,  80,  90, 128); // alpha 0.5
		constexpr DebugColor axisYCol = DebugColorRGBA(220,  60,  60, 128); // x=0 line: red,   alpha 0.5
		constexpr DebugColor axisXCol = DebugColorRGBA( 50, 200,  80, 128); // y=0 line: green, alpha 0.5

		// Vertical grid lines (constant x)
		const int xStart = static_cast<int>(std::ceilf(worldL / step));
		const int xEnd   = static_cast<int>(std::floorf(worldR / step));
		for (int k = xStart; k <= xEnd && (k - xStart) < MAX_GRID_LINES; ++k)
		{
			const float wx    = static_cast<float>(k) * step;
			const bool  isAxis = (k == 0);
			debugDraw.DrawLine(
				{ wx, worldB }, { wx, worldT },
				isAxis ? axisYCol : gridCol,
				isAxis ? AXIS_LINE_THICKNESS : 1.0f);
		}

		// Horizontal grid lines (constant y)
		const int yStart = static_cast<int>(std::ceilf(worldB / step));
		const int yEnd   = static_cast<int>(std::floorf(worldT / step));
		for (int k = yStart; k <= yEnd && (k - yStart) < MAX_GRID_LINES; ++k)
		{
			const float wy    = static_cast<float>(k) * step;
			const bool  isAxis = (k == 0);
			debugDraw.DrawLine(
				{ worldL, wy }, { worldR, wy },
				isAxis ? axisXCol : gridCol,
				isAxis ? AXIS_LINE_THICKNESS : 1.0f);
		}
	}

	// ── Entity picking ─────────────────────────────────────────────────────────────

	EntityId PickSpriteEntity(
		const CScene& scene,
		const Vector2<float>& worldPoint)
	{
		EntityId pickedEntity    = INVALID_ENTITY_ID;
		std::int32_t pickedOrder = std::numeric_limits<std::int32_t>::min();

		scene.ForEach<GameObject, Transform2D, SpriteRenderer2D>(
			[&](EntityId entity, const GameObject& go,
			    const Transform2D&, const SpriteRenderer2D& sprite)
			{
				if (false == go.IsActive || false == sprite.IsEnabled)
				{
					return;
				}

				const Matrix3x2 spriteLocal = Matrix3x2::Transform(sprite.Offset, 0.0f, sprite.Size);
				Matrix3x2 inv;
				if (false == (spriteLocal * GetWorldTransform(scene, entity)).TryInvert(inv))
				{
					return;
				}

				const Vector2<float> local = inv.TransformPoint(worldPoint);
				if (local.x < -0.5f || local.x > 0.5f || local.y < -0.5f || local.y > 0.5f)
				{
					return;
				}

				if (INVALID_ENTITY_ID == pickedEntity || sprite.SortOrder >= pickedOrder)
				{
					pickedEntity = entity;
					pickedOrder  = sprite.SortOrder;
				}
			});

		return pickedEntity;
	}
}

// ── CSceneViewTool ─────────────────────────────────────────────────────────────

void CSceneViewTool::SetEditorCamera(float x, float y, float size)
{
	m_targetCameraPos  = Vector2<float>(x, y);
	m_cameraPos        = Vector2<float>(x, y); // 즉시 이동 (보간 없이)
	m_targetCameraSize = (size > 0.0f) ? size : 5.0f;
	m_cameraSize       = m_targetCameraSize;
}

void CSceneViewTool::FocusOnEntity(EntityId entity, const CScene& scene)
{
	if (INVALID_ENTITY_ID == entity)
	{
		return;
	}

	// 월드 공간 위치 계산
	const Matrix3x2      worldTransform = GetWorldTransform(scene, entity);
	const Vector2<float> worldPos       = worldTransform.TransformPoint(Vector2<float>(0.0f, 0.0f));

	// 월드 공간 스케일 추출
	const float scaleX = std::sqrt(
		worldTransform.M11 * worldTransform.M11 + worldTransform.M12 * worldTransform.M12);
	const float scaleY = std::sqrt(
		worldTransform.M21 * worldTransform.M21 + worldTransform.M22 * worldTransform.M22);

	// 오브젝트의 월드 반경 추정 (우선순위: Sprite > Collider > Transform Scale)
	float halfExtent = 0.5f;

	const SpriteRenderer2D* sprite = scene.GetComponent<SpriteRenderer2D>(entity);
	if (sprite && sprite->IsEnabled)
	{
		// 스프라이트 크기 × 월드 스케일
		const float wx = sprite->Size.x * scaleX;
		const float wy = sprite->Size.y * scaleY;
		halfExtent = std::max(wx, wy) * 0.5f;
	}
	else
	{
		// 변환 스케일로 대체
		halfExtent = std::max(scaleX, scaleY) * 0.5f;
	}

	// 카메라 Size = 반경 × 여백 배수 (2.5 → 오브젝트가 화면의 약 40% 차지)
	constexpr float FOCUS_PADDING = 2.5f;
	const float newSize = std::clamp(halfExtent * FOCUS_PADDING, 0.5f, 1000.0f);

	// 스무딩 타겟만 변경 — 실제 카메라는 보간으로 부드럽게 이동
	m_targetCameraPos  = worldPos;
	m_targetCameraSize = newSize;
}

void CSceneViewTool::OnCreate()
{
	SetTitle("SceneView");
}

void CSceneViewTool::OnDestroy()
{
}

void CSceneViewTool::OnUpdate()
{
}

void CSceneViewTool::OnRenderStay()
{
	ImVec2 vpSize = ImGui::GetContentRegionAvail();
	vpSize.x = std::max(vpSize.x, 1.0f);
	vpSize.y = std::max(vpSize.y, 1.0f);

	// ── Smooth camera interpolation ────────────────────────────────────────────────
	// Exponential decay lerp: camera reaches ~99% of target in ≈0.46s at SPEED=10.
	// Tune CAMERA_SMOOTH_SPEED at the top of this file.
	{
		const float dt    = std::clamp(ImGui::GetIO().DeltaTime, 0.001f, 0.1f);
		const float alpha = 1.0f - std::expf(-CAMERA_SMOOTH_SPEED * dt);
		m_cameraPos.x  += (m_targetCameraPos.x  - m_cameraPos.x)  * alpha;
		m_cameraPos.y  += (m_targetCameraPos.y  - m_cameraPos.y)  * alpha;
		m_cameraSize   += (m_targetCameraSize   - m_cameraSize)   * alpha;
	}

	// ── Push smoothed camera to ImEditor & request RT ─────────────────────────────
	// These must be called BEFORE OnPrepareRender() (which happens in the RenderFrame
	// phase, after all module Update/OnRenderStay calls).
	if (Editor::ImEditor)
	{
		Editor::ImEditor->SetSceneViewCamera(m_cameraPos.x, m_cameraPos.y, m_cameraSize);
		Editor::ImEditor->RequestSceneViewRenderTarget(
			static_cast<std::uint32_t>(vpSize.x),
			static_cast<std::uint32_t>(vpSize.y));
	}

	// ── Submit debug draw commands ────────────────────────────────────────────────
	// These run before OnPrepareRender(), so CImEditor can render them into the RT.
	if (Core::DebugDraw2D.IsValid())
	{
		const float aspect = GetAspect(vpSize);

		// Grid (world-space lines → rendered GPU-side into the scene RT)
		SubmitGrid(*Core::DebugDraw2D, m_cameraPos.x, m_cameraPos.y, m_cameraSize, aspect);

		// Collider / physics debug overlays + Camera2D frustum visualization
		if (Core::SceneManager)
		{
			SafePtr<CScene> scene = Core::SceneManager->GetActiveScene();
			if (scene)
			{
				// 프로젝트 해상도를 가져와 Camera2D 뷰 프러스텀을 정확히 그립니다.
				float resW = 0.0f;
				float resH = 0.0f;
				if (Editor::ImEditor)
				{
					SafePtr<CProjectManager> pm = Editor::ImEditor->GetProjectManager();
					if (pm && pm->IsProjectLoaded())
					{
						resW = static_cast<float>(pm->GetResolutionWidth());
						resH = static_cast<float>(pm->GetResolutionHeight());
					}
					else
					{
						// 프로젝트 없음 → 현재 GameView RT 크기로 폴백
						const std::uint32_t gvW = Editor::ImEditor->GetGameViewWidth();
						const std::uint32_t gvH = Editor::ImEditor->GetGameViewHeight();
						if (gvW > 0 && gvH > 0)
						{
							resW = static_cast<float>(gvW);
							resH = static_cast<float>(gvH);
						}
					}
				}

				SceneDebugDraw::Submit(*scene, *Core::DebugDraw2D,
				                       Editor::GetSelectedEntity(), resW, resH);
			}
		}
	}

	// ── ImGui layer: input receiver + RT image + text overlay ────────────────────
	const ImVec2 vpMin = ImGui::GetCursorScreenPos();
	ImDrawList* dl     = ImGui::GetWindowDrawList();

	// InvisibleButton is the primary input target (covers the full viewport).
	ImGui::InvisibleButton("##SceneViewInput", vpSize,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

	// ── Layer 1: Solid background (drawn before RT so it shows through empty areas)
	dl->AddRectFilled(vpMin, vpMin + vpSize, IM_COL32(26, 28, 32, 255));

	// ── Layer 2: Scene RT (already contains sprites + debug geometry rendered GPU-side)
	void* texID = Editor::ImEditor ? Editor::ImEditor->GetSceneViewTextureID() : nullptr;
	if (texID)
	{
		dl->AddImage(reinterpret_cast<ImTextureID>(texID), vpMin, vpMin + vpSize,
		             ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
	}

	// ── Input: right-click pan, scroll zoom, left-click pick ─────────────────────
	// All input modifies TARGET camera values; smoothing handles the display.
	const bool isHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	const bool isActive  = ImGui::IsItemActive();

	// Right-click drag → pan (use TARGET size so drag speed is consistent)
	if (isActive && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f))
	{
		const ImVec2 delta  = ImGui::GetIO().MouseDelta;
		const float  aspect = GetAspect(vpSize);
		m_targetCameraPos.x -= delta.x / vpSize.x * 2.0f * m_targetCameraSize * aspect;
		m_targetCameraPos.y += delta.y / vpSize.y * 2.0f * m_targetCameraSize;
	}

	// Scroll wheel → zoom centred on cursor
	// All math uses TARGET space so the anchor point stays exact regardless of
	// how far the smoothed camera has caught up.
	if (isHovered)
	{
		const float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0.0f)
		{
			const ImVec2 mousePos = ImGui::GetIO().MousePos;

			// World point under the cursor in TARGET camera space (pre-zoom).
			const Vector2<float> worldBefore =
				ViewportToWorld(mousePos, vpMin, vpSize, m_targetCameraPos, m_targetCameraSize);

			m_targetCameraSize *= std::powf(0.85f, wheel);
			m_targetCameraSize  = std::clamp(m_targetCameraSize, 0.01f, 2000.0f);

			// Where does the same pixel land after the zoom?
			const Vector2<float> worldAfter =
				ViewportToWorld(mousePos, vpMin, vpSize, m_targetCameraPos, m_targetCameraSize);

			// Shift target so the world point stays pinned under the cursor.
			m_targetCameraPos.x += worldBefore.x - worldAfter.x;
			m_targetCameraPos.y += worldBefore.y - worldAfter.y;
		}
	}

	// Left-click → entity picking (only when not right-dragging)
	// Double-click → 추가로 포커싱
	const bool isLeftClicked =
		ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
		!ImGui::IsMouseDragging(ImGuiMouseButton_Right);
	const bool isDoubleClicked =
		isLeftClicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

	if (isLeftClicked && Core::SceneManager)
	{
		SafePtr<CScene> scene = Core::SceneManager->GetActiveScene();
		if (scene)
		{
			const ImVec2 mousePos = ImGui::GetIO().MousePos;
			const Vector2<float> worldPoint =
				ViewportToWorld(mousePos, vpMin, vpSize, m_cameraPos, m_cameraSize);
			const EntityId picked = PickSpriteEntity(*scene, worldPoint);
			if (INVALID_ENTITY_ID != picked)
			{
				Editor::SelectEntity(picked);
				// 더블클릭 → 선택된 오브젝트로 카메라 포커싱
				if (isDoubleClicked)
				{
					FocusOnEntity(picked, *scene);
				}
			}
			else
			{
				Editor::ClearSelection();
			}
		}
	}

	// ── Text overlay (topmost layer, via ImGui DrawList) ──────────────────────────
	const bool hasScene = Core::SceneManager.IsValid() && Core::SceneManager->GetActiveScene().IsValid();
	const ImVec2 textPos = vpMin + ImVec2(12.0f, 10.0f);
	dl->AddText(textPos, IM_COL32(210, 216, 224, 255),
	            hasScene ? "Active Scene" : "No Active Scene");

	char selText[96] = {};
	if (INVALID_ENTITY_ID == Editor::GetSelectedEntity())
	{
		std::snprintf(selText, sizeof(selText), "Selected: None");
	}
	else
	{
		std::snprintf(selText, sizeof(selText), "Selected: %llu",
		              static_cast<unsigned long long>(Editor::GetSelectedEntity()));
	}
	dl->AddText(textPos + ImVec2(0.0f, 20.0f), IM_COL32(150, 158, 170, 255), selText);

	char camText[128] = {};
	std::snprintf(camText, sizeof(camText), "Cam (%.2f, %.2f) | Size %.2f",
	              m_cameraPos.x, m_cameraPos.y, m_cameraSize);
	dl->AddText(textPos + ImVec2(0.0f, 40.0f), IM_COL32(130, 140, 155, 200), camText);

	// ── 선택된 카메라의 뷰포트 영역 오버레이 (초록색 인디케이터) ─────────────────
	// Camera2D가 선택되어 있을 때, 해당 카메라의 Position/Size (RT 상의 화면 공간)를
	// 우측 하단에 미니 다이어그램으로 렌더링합니다.
	// 이 사각형은 주황색 월드-공간 프러스텀과는 별개로, 최종 RT 상의 레이아웃을 나타냅니다.
	{
		const EntityId selected = Editor::GetSelectedEntity();
		if (INVALID_ENTITY_ID != selected && Core::SceneManager)
		{
			SafePtr<CScene> sceneForVP = Core::SceneManager->GetActiveScene();
			if (sceneForVP)
			{
				const Camera2D* cam = sceneForVP->GetComponent<Camera2D>(selected);
				if (cam)
				{
					// 프로젝트 해상도 취득
					float resW = 1920.0f;
					float resH = 1080.0f;
					if (Editor::ImEditor)
					{
						SafePtr<CProjectManager> pm = Editor::ImEditor->GetProjectManager();
						if (pm && pm->IsProjectLoaded())
						{
							resW = static_cast<float>(pm->GetResolutionWidth());
							resH = static_cast<float>(pm->GetResolutionHeight());
						}
					}

					// Layout2D → 픽셀 → 정규화 [0,1]
					const Vector2<float> posPixel  = cam->Position.Resolve(resW, resH);
					      Vector2<float> sizePixel = cam->Size.Resolve(resW, resH);
					if (sizePixel.x < 1.0f) sizePixel.x = resW;
					if (sizePixel.y < 1.0f) sizePixel.y = resH;

					const float normX = posPixel.x  / resW;
					const float normY = posPixel.y  / resH;
					const float normW = sizePixel.x / resW;
					const float normH = sizePixel.y / resH;

					// 인디케이터 크기 — RT 비율을 유지, 최대 너비 160 px
					const float indW   = 160.0f;
					const float indH   = indW * (resH / resW);
					const float margin = 12.0f;

					const ImVec2 indMin(
						vpMin.x + vpSize.x - indW - margin,
						vpMin.y + vpSize.y - indH - margin);
					const ImVec2 indMax(indMin.x + indW, indMin.y + indH);

					// 클리핑 (SceneView 패널 안으로 제한)
					dl->PushClipRect(vpMin, vpMin + vpSize, true);

					// 인디케이터 배경 (RT 전체 영역)
					dl->AddRectFilled(indMin, indMax, IM_COL32(15, 18, 22, 210));
					dl->AddRect(indMin, indMax, IM_COL32(70, 80, 95, 180), 0.0f, 0, 1.0f);

					// 뷰포트 사각형 (초록색) — 이 카메라가 RT에서 차지하는 화면 영역
					const ImVec2 vpRectMin(
						indMin.x + normX * indW,
						indMin.y + normY * indH);
					const ImVec2 vpRectMax(
						vpRectMin.x + normW * indW,
						vpRectMin.y + normH * indH);

					dl->AddRectFilled(vpRectMin, vpRectMax, IM_COL32(50, 200, 80, 45));
					dl->AddRect(vpRectMin, vpRectMax,
					            IM_COL32(80, 230, 110, 230), 0.0f, 0, 1.5f);

					// 라벨
					dl->AddText(ImVec2(indMin.x, indMin.y - 18.0f),
					            IM_COL32(80, 230, 110, 220),
					            Utillity::U8(u8"뷰포트 (선택된 카메라)"));

					dl->PopClipRect();
				}
			}
		}
	}
}
