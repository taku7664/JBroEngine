#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "ThirdParty/imgui/imgui.h"
#include "Utillity/Math/Vector2T.h"

namespace CanvasViewCoordinates
{
	float GetAspect(const ImVec2& viewportSize);
	Vector2 ViewportToWorld(
		const ImVec2& viewportPoint,
		const ImVec2& viewportMin,
		const ImVec2& viewportSize,
		const Vector2& cameraPosition,
		float cameraSize);
	ImVec2 WorldToViewport(
		const Vector2& worldPoint,
		const ImVec2& viewportMin,
		const ImVec2& viewportSize,
		const Vector2& cameraPosition,
		float cameraSize);
}

#endif
