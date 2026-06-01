#pragma once

#include "Core/Asset/AssetTypes.h"
#include "Core/Renderer/RendererTypes.h"
#include "Utillity/Math/Vector2T.h"

#include <cstdint>

class IRenderMaterial;
class IRenderMesh;

struct SpriteRenderer2D
{
	bool IsEnabled = true;
	AssetGuid SpriteGuid = INVALID_ASSET_GUID;
	AssetGuid MaterialGuid = INVALID_ASSET_GUID;
	// Mesh/Material 은 SpriteRenderSystem 이 매 프레임 채우는 런타임 캐시입니다.
	// 직렬화/복사 대상이 아닙니다.
	SafePtr<IRenderMesh> Mesh;
	SafePtr<IRenderMaterial> Material;
	Vector2 Size = Vector2(1.0f, 1.0f);
	Vector2 Offset = Vector2(0.0f, 0.0f);
	float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	std::int32_t SortOrder = 0;
	RenderLayerMask LayerMask = 0xffffffffu;
};
