#pragma once

#include <cstdint>

#include "Utillity/SafePtr.h"
#include "Utillity/Matrix3x2.h"

class IRHIDevice;
class IRenderMesh;
class IRenderMaterial;

using RenderLayerMask = std::uint32_t;

enum class ERenderQueue
{
	Background,
	Opaque,
	Transparent,
	Overlay
};

struct RendererDesc
{
	SafePtr<IRHIDevice> RHIDevice;
};

struct RenderItem
{
	SafePtr<IRenderMesh> Mesh;
	SafePtr<IRenderMaterial> Material;
	ERenderQueue Queue = ERenderQueue::Opaque;
	RenderLayerMask LayerMask = 0xffffffffu;
	Matrix3x2 Transform;
	float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	std::int32_t SortOrder = 0;
};
