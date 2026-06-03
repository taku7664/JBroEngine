#pragma once

#include "Core/Asset/AssetRef.h"
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
	// 스프라이트 자산 런타임 캐시 + strong ref — 사용 중 자산이 unload 되지 않게 보호.
	// SpriteGuid 가 바뀌면 캐시 무효화 (CachedSpriteGuid 와 비교).
	// 자산 픽셀이 reload 되면 m_pixelGeneration 이 증가 → CachedPixelGeneration 비교로 감지.
	// 직렬화/복사 대상이 아니다.
	AssetRef<IAsset> SpriteAssetCache;
	AssetGuid        CachedSpriteGuid = INVALID_ASSET_GUID;
	std::uint32_t    CachedPixelGeneration = 0;
	Vector2 Size = Vector2(1.0f, 1.0f);
	Vector2 Offset = Vector2(0.0f, 0.0f);
	float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	std::int32_t SortOrder = 0;
	RenderLayerMask LayerMask = 0xffffffffu;
};
