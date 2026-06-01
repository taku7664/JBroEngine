#pragma once

#include "Core/Asset/AssetTypes.h"
#include "Core/Renderer/RendererTypes.h"
#include "Utillity/Math/Vector2T.h"

#include <cstdint>

class CSpriteAsset;
class IAsset;
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
	// 스프라이트 자산 런타임 캐시 — SpriteGuid 가 가리키는 자산을 매 프레임 LoadAsset 으로
	// 다시 받지 않게 한다. CachedSpriteGuid 는 캐시가 어떤 GUID 에 대해 만들어졌는지의 표시이며,
	// SpriteGuid 가 바뀌었으면 캐시 무효화 신호로 쓴다. 직렬화/복사 대상이 아니다.
	SafePtr<IAsset> SpriteAssetCache;
	AssetGuid       CachedSpriteGuid = INVALID_ASSET_GUID;
	Vector2 Size = Vector2(1.0f, 1.0f);
	Vector2 Offset = Vector2(0.0f, 0.0f);
	float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	std::int32_t SortOrder = 0;
	RenderLayerMask LayerMask = 0xffffffffu;
};
