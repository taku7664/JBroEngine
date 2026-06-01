#pragma once

#include "Utillity/Math/Vector2T.h"

#include <vector>

class CPhysics2DGeometry final
{
public:
	static std::vector<Vector2> CreateBoxPoints(float width, float height);
	static std::vector<Vector2> CreateRegularPolygonPoints(float radius, std::uint32_t vertexCount);
};
