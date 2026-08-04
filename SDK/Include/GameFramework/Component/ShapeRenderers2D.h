#pragma once

#include "Core/Renderer/RendererTypes.h"
#include "GameFramework/Component/Component.h"
#include "Utillity/Math/Vector2T.h"
#include "Utillity/Types/Color.h"

#include <cstdint>

class Square2D final : public CComponent
{
	JBRO_COMPONENT(Square2D)
public:
	Vector2 Size = Vector2(1.0f, 1.0f);
	Vector2 Offset = Vector2(0.0f, 0.0f);
	bool FillEnabled = true;
	Color FillColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	bool OutlineEnabled = false;
	Color OutlineColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	float OutlineWidth = 0.05f;
	std::int32_t SortOrder = 0;
};

class Circle2D final : public CComponent
{
	JBRO_COMPONENT(Circle2D)
public:
	float Radius = 0.5f;
	std::uint32_t Segments = 64;
	Vector2 Offset = Vector2(0.0f, 0.0f);
	bool FillEnabled = true;
	Color FillColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	bool OutlineEnabled = false;
	Color OutlineColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	float OutlineWidth = 0.05f;
	std::int32_t SortOrder = 0;
};

class Polygon2D final : public CComponent
{
	JBRO_COMPONENT(Polygon2D)
public:
	float Radius = 0.5f;
	std::uint32_t VertexCount = 6;
	float StartAngle = 0.0f;
	Vector2 Offset = Vector2(0.0f, 0.0f);
	bool FillEnabled = true;
	Color FillColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	bool OutlineEnabled = false;
	Color OutlineColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	float OutlineWidth = 0.05f;
	std::int32_t SortOrder = 0;
};
