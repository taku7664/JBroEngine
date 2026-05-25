#pragma once

#include "Core/Asset/AssetTypes.h"
#include "Core/Renderer/RendererTypes.h"
#include "Utillity/Vector2T.h"

#include <cstdint>

class IRenderMaterial;
class IRenderMesh;

struct SpriteRenderer2D
{
	bool IsEnabled = true;
	AssetGuid SpriteGuid = INVALID_ASSET_GUID;
	AssetGuid MaterialGuid = INVALID_ASSET_GUID;
	SafePtr<IRenderMesh> Mesh;
	SafePtr<IRenderMaterial> Material;
	OwnerPtr<IRenderMaterial> RuntimeMaterial;
	Vector2<float> Size = Vector2<float>(1.0f, 1.0f);
	Vector2<float> Offset = Vector2<float>(0.0f, 0.0f);
	float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	std::int32_t SortOrder = 0;
	RenderLayerMask LayerMask = 0xffffffffu;
};
