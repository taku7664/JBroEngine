#include "pch.h"
#include "DebugDraw2D.h"

#include <algorithm>

// ── 전역 오버로드 (Entity = INVALID) ─────────────────────────────────────────

void CDebugDraw2D::DrawLine(
	const Vector2& a, const Vector2& b,
	DebugColor color, float thickness)
{
	DrawLine(INVALID_DEBUG_OBJECT_ID, a, b, color, thickness);
}

void CDebugDraw2D::DrawCircle(
	const Vector2& center, float radius,
	DebugColor color, float thickness, int segments)
{
	DrawCircle(INVALID_DEBUG_OBJECT_ID, center, radius, color, thickness, segments);
}

void CDebugDraw2D::DrawPolygon(
	const Vector2* points, int count,
	DebugColor color, float thickness)
{
	DrawPolygon(INVALID_DEBUG_OBJECT_ID, points, count, color, thickness);
}

// ── 엔티티 태깅 오버로드 ──────────────────────────────────────────────────────

void CDebugDraw2D::DrawLine(
	DebugObjectId entity,
	const Vector2& a, const Vector2& b,
	DebugColor color, float thickness)
{
	DebugLine2D line;
	line.A         = a;
	line.B         = b;
	line.Color     = color;
	line.Thickness = thickness;
	line.Entity    = entity;
	m_lines.push_back(line);
}

void CDebugDraw2D::DrawCircle(
	DebugObjectId entity,
	const Vector2& center, float radius,
	DebugColor color, float thickness, int segments)
{
	DebugCircle2D circle;
	circle.Center    = center;
	circle.Radius    = radius;
	circle.Color     = color;
	circle.Thickness = thickness;
	circle.Segments  = std::max(segments, 3);
	circle.Entity    = entity;
	m_circles.push_back(circle);
}

void CDebugDraw2D::DrawPolygon(
	DebugObjectId entity,
	const Vector2* points, int count,
	DebugColor color, float thickness)
{
	if (nullptr == points || count < 2)
	{
		return;
	}

	for (int i = 0; i < count; ++i)
	{
		DrawLine(entity, points[i], points[(i + 1) % count], color, thickness);
	}
}

void CDebugDraw2D::Clear()
{
	m_lines.clear();
	m_circles.clear();
}
