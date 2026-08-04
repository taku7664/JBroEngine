#pragma once

#include "Core/Asset/AssetRef.h"
#include "Core/Asset/AssetTypes.h"
#include "Core/Renderer/RendererTypes.h"
#include "GameFramework/Component/Component.h"
#include "Utillity/Math/Vector2T.h"
#include "Utillity/Types/Color.h"

#include <cstdint>

class CSpriteAsset;
class IAsset;
class IRenderMaterial;
class IRenderMesh;

class SpriteRenderer2D final : public CComponent
{
	JBRO_COMPONENT(SpriteRenderer2D)
public:
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
	// 좌우/상하 반전 — 월드 크기 부호를 뒤집어 쿼드를 미러링한다(별도 텍스처 불필요).
	bool FlipX = false;
	bool FlipY = false;
	// 스프라이트시트 프레임 인덱스 — 자산의 SpriteFrame 목록에서 표시할 프레임.
	// 슬라이스 없는 자산은 무시(항상 전체). SpriteAnimator2D 가 매 프레임 갱신할 수 있다.
	std::uint32_t FrameIndex = 0;
	::Color Color = { 1.0f, 1.0f, 1.0f, 1.0f };
	std::int32_t SortOrder = 0;
	// 그림자 캐스터 — 켜면 이 스프라이트의 실루엣(알파)이 CastShadows 라이트의 그림자를 만든다.
	bool CastShadow = false;
};
