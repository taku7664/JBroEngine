#pragma once

#include <cstdint>

#include "Utillity/Pointer/SafePtr.h"
#include "Utillity/Math/Matrix3x2.h"

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
	// 이 렌더 아이템을 제출한 오브젝트의 불투명 식별자(CGameObject::GetId).
	// 필터 렌더링/아웃라인 마스크/픽킹에 사용. 0 = 오브젝트 무관 아이템.
	std::uint64_t Entity = 0;
};
