#pragma once

#include <cstdint>

#include "Utillity/Pointer/SafePtr.h"
#include "Utillity/Math/Matrix3x2.h"

class IRHIDevice;
class IRHIGraphicsPipeline;
class IRHISampler;
class IRHITexture;
class IRenderMesh;
class IRenderMaterial;

using RenderLayerMask = std::uint32_t;

// 렌더 아이템을 제출한 오브젝트의 불투명 키(GameFramework CGameObject 의 주소).
// Core 렌더러는 이 키로 집합 비교(필터/아웃라인 마스크)만 하고 절대 역참조하지 않으므로
// GameFramework 타입을 몰라도 된다(void*). 프레임 한정 식별 — 저장/직렬화 대상 아님.
using RenderObjectId = const void*;

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
	SafePtr<IRHIGraphicsPipeline> Pipeline;
	SafePtr<IRHITexture> Texture;
	SafePtr<IRHISampler> Sampler;
	ERenderQueue Queue = ERenderQueue::Opaque;
	RenderLayerMask LayerMask = 0xffffffffu;
	Matrix3x2 Transform;
	float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	std::int32_t SortOrder = 0;
	// 이 렌더 아이템을 제출한 오브젝트의 불투명 키(CGameObject 주소).
	// 필터 렌더링/아웃라인 마스크에 사용(집합 비교 전용, 역참조 금지). nullptr = 무관 아이템.
	RenderObjectId Entity = nullptr;
};
