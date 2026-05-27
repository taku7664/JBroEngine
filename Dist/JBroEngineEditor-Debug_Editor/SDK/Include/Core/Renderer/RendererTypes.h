#pragma once

#include <cstdint>

#include "Utillity/SafePtr.h"
#include "Utillity/Matrix3x2.h"
#include "GameFramework/ECS/EntityTypes.h"

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
	// 이 렌더 아이템을 제출한 엔티티 (필터 렌더링/아웃라인 마스크 등에 사용).
	// INVALID_ENTITY_ID = 엔티티와 무관한 아이템.
	EntityId Entity = INVALID_ENTITY_ID;
};
