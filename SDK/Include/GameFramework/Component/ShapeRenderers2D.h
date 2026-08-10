#pragma once

#include "Core/Renderer/RendererTypes.h"
#include "GameFramework/Component/Renderer2DComponent.h"
#include "Utillity/Math/Vector2T.h"
#include "Utillity/Types/Color.h"
#include "Utillity/Types/Radian.h"

#include <cstdint>

class CReflectionRegistry;

// 도형 렌더러의 경계는 **채움 도형의 기하**다 — OutlineWidth 는 포함하지 않는다
// (외곽선은 도형을 따라 그려지는 장식이고, 두께를 경계에 넣으면 판정이 스타일에 끌려간다).

class Square2D final : public CRenderer2DComponent
{
	JBRO_COMPONENT_BASE(Square2D, CRenderer2DComponent)
public:
	const Vector2& GetSize() const { return m_size; }
	void SetSize(const Vector2& size) { m_size = size; MarkBoundsDirty(); }

	bool FillEnabled = true;
	Color FillColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	bool OutlineEnabled = false;
	Color OutlineColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	float OutlineWidth = 0.05f;

protected:
	void ComputeLocalBounds(Rect& outBounds) const override;

private:
	// 리플렉션 등록만 raw offset 을 본다 — 그래서 여기만 friend 다.
	friend void RegisterBuiltinComponents(CReflectionRegistry&);

	Vector2 m_size = Vector2(1.0f, 1.0f);
};

class Circle2D final : public CRenderer2DComponent
{
	JBRO_COMPONENT_BASE(Circle2D, CRenderer2DComponent)
public:
	float GetRadius() const { return m_radius; }
	void SetRadius(float radius) { m_radius = radius; MarkBoundsDirty(); }

	// Segments 는 테셀레이션 품질일 뿐 경계를 바꾸지 않는다 → 공개 유지.
	std::uint32_t Segments = 64;
	bool FillEnabled = true;
	Color FillColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	bool OutlineEnabled = false;
	Color OutlineColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	float OutlineWidth = 0.05f;

protected:
	void ComputeLocalBounds(Rect& outBounds) const override;

private:
	friend void RegisterBuiltinComponents(CReflectionRegistry&);

	float m_radius = 0.5f;
};

class Polygon2D final : public CRenderer2DComponent
{
	JBRO_COMPONENT_BASE(Polygon2D, CRenderer2DComponent)
public:
	float GetRadius() const { return m_radius; }
	void SetRadius(float radius) { m_radius = radius; MarkBoundsDirty(); }

	std::uint32_t GetVertexCount() const { return m_vertexCount; }
	void SetVertexCount(std::uint32_t vertexCount) { m_vertexCount = vertexCount; MarkBoundsDirty(); }

	Radian GetStartAngle() const { return m_startAngle; }
	void SetStartAngle(Radian startAngle) { m_startAngle = startAngle; MarkBoundsDirty(); }

	bool FillEnabled = true;
	Color FillColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	bool OutlineEnabled = false;
	Color OutlineColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	float OutlineWidth = 0.05f;

protected:
	// 정다각형이라 꼭짓점을 실제로 돌며 min/max 를 낸다(외접원으로 근사하지 않는다 —
	// 삼각형처럼 꼭짓점이 적을수록 근사 오차가 커서 판정이 눈에 띄게 헐거워진다).
	void ComputeLocalBounds(Rect& outBounds) const override;

private:
	friend void RegisterBuiltinComponents(CReflectionRegistry&);

	float m_radius = 0.5f;
	std::uint32_t m_vertexCount = 6;
	Radian m_startAngle = 0.0f;
};
