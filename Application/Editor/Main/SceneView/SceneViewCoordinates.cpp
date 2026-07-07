#include "pch.h"
#include "SceneViewCoordinates.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

float SceneViewCoordinates::GetAspect(const ImVec2& viewportSize)
{
	return viewportSize.y > 0.0f ? viewportSize.x / viewportSize.y : 1.0f;
}

Vector2 SceneViewCoordinates::ViewportToWorld(
	const ImVec2& viewportPoint,
	const ImVec2& viewportMin,
	const ImVec2& viewportSize,
	const Vector2& cameraPosition,
	float cameraSize)
{
	const float ndcX = ((viewportPoint.x - viewportMin.x) / viewportSize.x) * 2.0f - 1.0f;
	const float ndcY = 1.0f - ((viewportPoint.y - viewportMin.y) / viewportSize.y) * 2.0f;
	const float aspect = GetAspect(viewportSize);
	return Vector2(
		ndcX * cameraSize * aspect + cameraPosition.x,
		ndcY * cameraSize + cameraPosition.y);
}

ImVec2 SceneViewCoordinates::WorldToViewport(
	const Vector2& worldPoint,
	const ImVec2& viewportMin,
	const ImVec2& viewportSize,
	const Vector2& cameraPosition,
	float cameraSize)
{
	const float aspect = GetAspect(viewportSize);
	const float ndcX = (worldPoint.x - cameraPosition.x) / (cameraSize * aspect);
	const float ndcY = (worldPoint.y - cameraPosition.y) / cameraSize;
	return ImVec2(
		viewportMin.x + (ndcX + 1.0f) * 0.5f * viewportSize.x,
		viewportMin.y + (1.0f - ndcY) * 0.5f * viewportSize.y);
}

#endif
