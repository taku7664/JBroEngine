#include "pch.h"
#include "CanvasDebugDrawSystem.h"

#include "Core/Debug/DebugDraw2D.h"
#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Canvas/CanvasTransformUtils.h"

#include <cmath>

namespace
{
	// Camera2D frustum
	constexpr DebugColor CAMERA_COLOR = DebugColorRGBA(255, 220, 50, 220);
	constexpr float      CAMERA_LINE_THICKNESS = 1.5f;

	void DrawCameraFrustum(
		IDebugDraw2D& debugDraw,
		DebugObjectId entity,
		float posX, float posY,
		float orthoSize, float cameraAspect,
		float cosR, float sinR,
		DebugColor color)
	{
		const float halfW = orthoSize * cameraAspect;
		const float halfH = orthoSize;

		const float lx[4] = { -halfW,  halfW,  halfW, -halfW };
		const float ly[4] = { -halfH, -halfH,  halfH,  halfH };

		Vector2 corners[4];
		for (int i = 0; i < 4; ++i)
		{
			corners[i].x = posX + lx[i] * cosR - ly[i] * sinR;
			corners[i].y = posY + lx[i] * sinR + ly[i] * cosR;
		}

		for (int i = 0; i < 4; ++i)
			debugDraw.DrawLine(entity, corners[i], corners[(i + 1) % 4], color, CAMERA_LINE_THICKNESS);

		const float crossSize = std::max(halfW, halfH) * 0.05f;
		const Vector2 right  = { cosR * crossSize,  sinR * crossSize };
		const Vector2 up     = { -sinR * crossSize, cosR * crossSize };
		const Vector2 origin = { posX, posY };
		debugDraw.DrawLine(entity, origin - right, origin + right, color, CAMERA_LINE_THICKNESS);
		debugDraw.DrawLine(entity, origin - up,    origin + up,    color, CAMERA_LINE_THICKNESS);
	}
}

void CanvasDebugDraw::Submit(
	const CGameCanvas&      canvas,
	IDebugDraw2D&      debugDraw,
	const CGameObject* /*selectedObject*/,
	float              resW,
	float              resH,
	const char*        /*activeCompType*/)
{
	// ── 콜라이더(Circle / Polygon) 렌더링은 CanvasViewTool Layer 2.8 의 ImGui 경로로 이전됨 ──
	// CanvasDebugDrawSystem 은 카메라 프러스텀만 담당한다.

	// ── Camera2D frustum rectangles ────────────────────────────────────────────
	if (resW <= 0.0f || resH <= 0.0f)
		return;

	canvas.ForEach<Camera2D>(
		[&](const Camera2D& cam)
		{
			const CGameObject* owner = cam.GetOwner().TryGet();
			if (nullptr == owner || false == owner->IsActive || false == cam.IsEnabled())
				return;

			const DebugObjectId entity = owner; // 불투명 키(주소). 디버그 렌더 집합 비교용.
			const Matrix3x2 wt = GetWorldTransform(*owner);

			const float posX = wt.Dx;
			const float posY = wt.Dy;
			const float cosR = (wt.M11 != 0.0f || wt.M12 != 0.0f)
				? wt.M11 / std::sqrt(wt.M11 * wt.M11 + wt.M12 * wt.M12)
				: 1.0f;
			const float sinR = (wt.M11 != 0.0f || wt.M12 != 0.0f)
				? wt.M12 / std::sqrt(wt.M11 * wt.M11 + wt.M12 * wt.M12)
				: 0.0f;

			const float baseAspect = resW / resH;
			const float sX = std::sqrt(wt.M11 * wt.M11 + wt.M12 * wt.M12);
			const float sY = std::sqrt(wt.M21 * wt.M21 + wt.M22 * wt.M22);
			const float safeScaleX = std::max(sX, 0.0001f);
			const float safeScaleY = std::max(sY, 0.0001f);

			const float baseOrtho    = cam.OrthographicSize > 0.0f ? cam.OrthographicSize : 5.0f;
			const float orthoSize    = baseOrtho * safeScaleY;
			const float cameraAspect = baseAspect * (safeScaleX / safeScaleY);

			DrawCameraFrustum(debugDraw, entity, posX, posY, orthoSize, cameraAspect, cosR, sinR, CAMERA_COLOR);
		});
}
