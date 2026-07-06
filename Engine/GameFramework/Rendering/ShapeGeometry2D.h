#pragma once

#include "Utillity/Math/Vector2T.h"

#include <cstdint>
#include <vector>

struct ShapeVertex2D
{
	float Position[2];
	float UV[2];
};

struct ShapeMeshData2D
{
	std::vector<ShapeVertex2D> Vertices;
	std::vector<std::uint32_t> Indices;

	bool IsEmpty() const { return Vertices.empty() || Indices.empty(); }
};

struct ShapeGeometry2D
{
	ShapeMeshData2D Fill;
	ShapeMeshData2D Outline;
};

class CShapeGeometryBuilder2D
{
public:
	static ShapeGeometry2D BuildRectangle(const Vector2& size, bool fillEnabled, bool outlineEnabled, float outlineWidth);
	static ShapeGeometry2D BuildRegularPolygon(float radius, std::uint32_t vertexCount, float startAngle,
		bool fillEnabled, bool outlineEnabled, float outlineWidth);
};
