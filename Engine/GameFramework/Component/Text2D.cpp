#include "pch.h"
#include "Text2D.h"

void Text2D::SetShapedBounds(float centerX, float centerY, float width, float height)
{
	if (m_hasShapedBounds
		&& m_shapedCenter.x == centerX
		&& m_shapedCenter.y == centerY
		&& m_shapedWidth == width
		&& m_shapedHeight == height)
	{
		return;
	}

	m_shapedCenter = Vector2(centerX, centerY);
	m_shapedWidth = width;
	m_shapedHeight = height;
	m_hasShapedBounds = true;
	MarkBoundsDirty();
}

void Text2D::ComputeLocalBounds(Rect& outBounds) const
{
	if (false == m_hasShapedBounds || m_shapedWidth <= 0.0f || m_shapedHeight <= 0.0f)
	{
		// 아직 셰이핑되지 않았거나 빈 텍스트 → 경계 미상(빈 Rect).
		return;
	}

	const Vector2 center = m_offset + m_shapedCenter;
	const float halfWidth = m_shapedWidth * 0.5f;
	const float halfHeight = m_shapedHeight * 0.5f;
	outBounds = Rect(center.x - halfWidth, center.y - halfHeight, center.x + halfWidth, center.y + halfHeight);
}
