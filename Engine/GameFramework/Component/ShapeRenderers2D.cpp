#include "pch.h"
#include "ShapeRenderers2D.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float SHAPE_TWO_PI = 6.28318530718f;

	Rect MakeCenteredBounds(const Vector2& center, float halfWidth, float halfHeight)
	{
		// Rect 의 Top 은 최소 y, Bottom 은 최대 y (Renderer2DComponent.h 의 경계 규약).
		return Rect(center.x - halfWidth, center.y - halfHeight, center.x + halfWidth, center.y + halfHeight);
	}
}

void Square2D::ComputeLocalBounds(Rect& outBounds) const
{
	// 음수 크기는 미러링 표현이라 경계는 절대값으로 잡는다(CShapeGeometryBuilder2D 와 동일).
	outBounds = MakeCenteredBounds(m_offset, std::abs(m_size.x) * 0.5f, std::abs(m_size.y) * 0.5f);
}

void Circle2D::ComputeLocalBounds(Rect& outBounds) const
{
	const float radius = std::abs(m_radius);
	outBounds = MakeCenteredBounds(m_offset, radius, radius);
}

void Polygon2D::ComputeLocalBounds(Rect& outBounds) const
{
	const float radius = std::abs(m_radius);
	if (radius <= 0.0f)
	{
		// 반지름이 없으면 도형이 성립하지 않는다 → 경계 없음(빈 Rect 유지).
		return;
	}

	// CShapeRenderSystem::SubmitPolygon 과 **같은 클램프**를 써야 한다. 여기서 갈리면
	// VertexCount=2 일 때 화면엔 삼각형이 그려지는데 판정 영역만 비는 식으로 어긋난다.
	const std::uint32_t vertexCount = std::clamp(m_vertexCount, 3u, 256u);

	// 각도 배치도 CShapeGeometryBuilder2D::BuildPolygonPoints 를 그대로 따른다.
	float minX = 0.0f;
	float minY = 0.0f;
	float maxX = 0.0f;
	float maxY = 0.0f;
	for (std::uint32_t index = 0; index < vertexCount; ++index)
	{
		const float angle = m_startAngle + (SHAPE_TWO_PI * static_cast<float>(index)) / static_cast<float>(vertexCount);
		const float x = std::cos(angle) * radius;
		const float y = std::sin(angle) * radius;
		if (0 == index)
		{
			minX = maxX = x;
			minY = maxY = y;
			continue;
		}

		minX = std::min(minX, x);
		minY = std::min(minY, y);
		maxX = std::max(maxX, x);
		maxY = std::max(maxY, y);
	}

	outBounds = Rect(m_offset.x + minX, m_offset.y + minY, m_offset.x + maxX, m_offset.y + maxY);
}
