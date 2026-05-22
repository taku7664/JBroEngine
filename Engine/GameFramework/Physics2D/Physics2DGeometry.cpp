#include "pch.h"
#include "Physics2DGeometry.h"

#include <cmath>

std::vector<Vector2<float>> CPhysics2DGeometry::CreateBoxPoints(float width, float height)
{
	const float halfWidth = width * 0.5f;
	const float halfHeight = height * 0.5f;
	return {
		Vector2<float>(-halfWidth, -halfHeight),
		Vector2<float>(halfWidth, -halfHeight),
		Vector2<float>(halfWidth, halfHeight),
		Vector2<float>(-halfWidth, halfHeight)
	};
}

std::vector<Vector2<float>> CPhysics2DGeometry::CreateRegularPolygonPoints(float radius, std::uint32_t vertexCount)
{
	std::vector<Vector2<float>> points;
	if (vertexCount < 3 || radius <= 0.0f)
	{
		return points;
	}

	const float PI = 3.14159265358979323846f;
	points.reserve(vertexCount);
	for (std::uint32_t i = 0; i < vertexCount; ++i)
	{
		const float angle = (2.0f * PI * static_cast<float>(i)) / static_cast<float>(vertexCount);
		points.emplace_back(std::cos(angle) * radius, std::sin(angle) * radius);
	}
	return points;
}
