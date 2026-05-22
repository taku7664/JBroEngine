#pragma once

#include "Core/Asset/AssetTypes.h"
#include "Core/Renderer/RendererTypes.h"

#include <cstdint>

class IRenderMaterial;
class IRenderMesh;

struct SpriteRenderer2D
{
	AssetGuid SpriteGuid = INVALID_ASSET_GUID;
	AssetGuid MaterialGuid = INVALID_ASSET_GUID;
	SafePtr<IRenderMesh> Mesh;
	SafePtr<IRenderMaterial> Material;
	OwnerPtr<IRenderMaterial> RuntimeMaterial;
	float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	std::int32_t SortOrder = 0;
	RenderLayerMask LayerMask = 0xffffffffu;
};
