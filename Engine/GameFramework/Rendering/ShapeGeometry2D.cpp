#include "pch.h"
#include "ShapeGeometry2D.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float SHAPE_PI = 3.14159265358979323846f;
	constexpr float MIN_SIZE = 0.000001f;

	ShapeVertex2D MakeVertex(float x, float y)
	{
		return { { x, y }, { x + 0.5f, 0.5f - y } };
	}

	void BuildFan(const std::vector<Vector2>& points, ShapeMeshData2D& outMesh)
	{
		if (points.size() < 3)
		{
			return;
		}

		outMesh.Vertices.reserve(points.size() + 1);
		outMesh.Indices.reserve(points.size() * 3);
		outMesh.Vertices.push_back(MakeVertex(0.0f, 0.0f));
		for (const Vector2& point : points)
		{
			outMesh.Vertices.push_back(MakeVertex(point.x, point.y));
		}
		for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(points.size()); ++i)
		{
			const std::uint32_t next = (i + 1) % static_cast<std::uint32_t>(points.size());
			outMesh.Indices.push_back(0);
			outMesh.Indices.push_back(i + 1);
			outMesh.Indices.push_back(next + 1);
		}
	}

	void BuildRing(const std::vector<Vector2>& outer, const std::vector<Vector2>& inner, ShapeMeshData2D& outMesh)
	{
		if (outer.size() < 3 || outer.size() != inner.size())
		{
			return;
		}

		const std::uint32_t count = static_cast<std::uint32_t>(outer.size());
		outMesh.Vertices.reserve(count * 2);
		outMesh.Indices.reserve(count * 6);
		for (std::uint32_t i = 0; i < count; ++i)
		{
			outMesh.Vertices.push_back(MakeVertex(outer[i].x, outer[i].y));
			outMesh.Vertices.push_back(MakeVertex(inner[i].x, inner[i].y));
		}
		for (std::uint32_t i = 0; i < count; ++i)
		{
			const std::uint32_t next = (i + 1) % count;
			const std::uint32_t outerCurrent = i * 2;
			const std::uint32_t innerCurrent = outerCurrent + 1;
			const std::uint32_t outerNext = next * 2;
			const std::uint32_t innerNext = outerNext + 1;
			outMesh.Indices.insert(outMesh.Indices.end(), {
				outerCurrent, innerCurrent, innerNext,
				outerCurrent, innerNext, outerNext
			});
		}
	}

	std::vector<Vector2> BuildPolygonPoints(float radius, std::uint32_t count, float startAngle)
	{
		std::vector<Vector2> points;
		points.reserve(count);
		for (std::uint32_t i = 0; i < count; ++i)
		{
			const float angle = startAngle + (2.0f * SHAPE_PI * static_cast<float>(i)) / static_cast<float>(count);
			points.emplace_back(std::cos(angle) * radius, std::sin(angle) * radius);
		}
		return points;
	}
}

ShapeGeometry2D CShapeGeometryBuilder2D::BuildRectangle(const Vector2& size, bool fillEnabled, bool outlineEnabled, float outlineWidth)
{
	ShapeGeometry2D geometry;
	const float halfWidth = std::abs(size.x) * 0.5f;
	const float halfHeight = std::abs(size.y) * 0.5f;
	if (halfWidth <= MIN_SIZE || halfHeight <= MIN_SIZE)
	{
		return geometry;
	}

	const std::vector<Vector2> outer = {
		{ -halfWidth, -halfHeight },
		{ halfWidth, -halfHeight },
		{ halfWidth, halfHeight },
		{ -halfWidth, halfHeight }
	};
	const float width = outlineEnabled ? std::max(0.0f, outlineWidth) : 0.0f;
	const float innerHalfWidth = std::max(0.0f, halfWidth - width);
	const float innerHalfHeight = std::max(0.0f, halfHeight - width);

	if (outlineEnabled && width > MIN_SIZE)
	{
		if (innerHalfWidth <= MIN_SIZE || innerHalfHeight <= MIN_SIZE)
		{
			BuildFan(outer, geometry.Outline);
		}
		else
		{
			const std::vector<Vector2> inner = {
				{ -innerHalfWidth, -innerHalfHeight },
				{ innerHalfWidth, -innerHalfHeight },
				{ innerHalfWidth, innerHalfHeight },
				{ -innerHalfWidth, innerHalfHeight }
			};
			BuildRing(outer, inner, geometry.Outline);
			if (fillEnabled)
			{
				BuildFan(inner, geometry.Fill);
			}
		}
	}
	else if (fillEnabled)
	{
		BuildFan(outer, geometry.Fill);
	}
	return geometry;
}

ShapeGeometry2D CShapeGeometryBuilder2D::BuildRegularPolygon(float radius, std::uint32_t vertexCount, float startAngle,
	bool fillEnabled, bool outlineEnabled, float outlineWidth)
{
	ShapeGeometry2D geometry;
	const float outerRadius = std::abs(radius);
	if (outerRadius <= MIN_SIZE || vertexCount < 3)
	{
		return geometry;
	}

	const std::vector<Vector2> outer = BuildPolygonPoints(outerRadius, vertexCount, startAngle);
	const float width = outlineEnabled ? std::max(0.0f, outlineWidth) : 0.0f;
	const float apothemFactor = std::cos(SHAPE_PI / static_cast<float>(vertexCount));
	const float innerRadius = apothemFactor > MIN_SIZE
		? std::max(0.0f, outerRadius - width / apothemFactor)
		: 0.0f;

	if (outlineEnabled && width > MIN_SIZE)
	{
		if (innerRadius <= MIN_SIZE)
		{
			BuildFan(outer, geometry.Outline);
		}
		else
		{
			const std::vector<Vector2> inner = BuildPolygonPoints(innerRadius, vertexCount, startAngle);
			BuildRing(outer, inner, geometry.Outline);
			if (fillEnabled)
			{
				BuildFan(inner, geometry.Fill);
			}
		}
	}
	else if (fillEnabled)
	{
		BuildFan(outer, geometry.Fill);
	}
	return geometry;
}
