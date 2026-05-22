#pragma once

#include "Utillity/Vector2T.h"

#include <vector>

class CPhysics2DGeometry final
{
public:
	static std::vector<Vector2<float>> CreateBoxPoints(float width, float height);
	static std::vector<Vector2<float>> CreateRegularPolygonPoints(float radius, std::uint32_t vertexCount);
};
