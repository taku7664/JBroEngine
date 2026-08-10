#pragma once

#include "Core/Asset/AssetRef.h"
#include "Core/Asset/AssetTypes.h"
#include "Core/Renderer/RendererTypes.h"
#include "GameFramework/Component/Renderer2DComponent.h"
#include "Utillity/Math/Vector2T.h"
#include "Utillity/Types/Color.h"

#include <cstdint>

class CSpriteAsset;
class CReflectionRegistry;
class IAsset;
class IRenderMaterial;
class IRenderMesh;

class SpriteRenderer2D final : public CRenderer2DComponent
{
	JBRO_COMPONENT_BASE(SpriteRenderer2D, CRenderer2DComponent)
public:
	const AssetGuid& GetSpriteGuid() const { return m_spriteGuid; }
	void SetSpriteGuid(const AssetGuid& guid) { m_spriteGuid = guid; MarkBoundsDirty(); }

	// 스프라이트 픽셀 크기에 곱해지는 배수다(절대 유닛 크기가 아니다).
	const Vector2& GetSize() const { return m_size; }
	void SetSize(const Vector2& size) { m_size = size; MarkBoundsDirty(); }

	const Vector2& GetOffset() const { return m_offset; }
	void SetOffset(const Vector2& offset) { m_offset = offset; MarkBoundsDirty(); }

	// 스프라이트시트 프레임 인덱스 — 자산의 SpriteFrame 목록에서 표시할 프레임.
	// 슬라이스 없는 자산은 무시(항상 전체). SpriteAnimator2D 가 매 프레임 갱신할 수 있다.
	std::uint32_t GetFrameIndex() const { return m_frameIndex; }
	void SetFrameIndex(std::uint32_t frameIndex) { m_frameIndex = frameIndex; MarkBoundsDirty(); }

	AssetGuid MaterialGuid = INVALID_ASSET_GUID;
	// 좌우/상하 반전 — 월드 크기 부호를 뒤집어 쿼드를 미러링한다(별도 텍스처 불필요).
	// 쿼드는 Offset 을 중심으로 그려지므로 반전해도 경계는 그대로다 → 세터로 닫지 않는다.
	bool FlipX = false;
	bool FlipY = false;
	::Color Color = { 1.0f, 1.0f, 1.0f, 1.0f };
	std::int32_t SortOrder = 0;
	// 그림자 캐스터 — 켜면 이 스프라이트의 실루엣(알파)이 CastShadows 라이트의 그림자를 만든다.
	bool CastShadow = false;

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
	// 자산의 유효 PPU(자산 PPU 가 0 이면 프로젝트 기본값). SpriteRenderSystem 이 주입한다.
	// 컴포넌트가 전역 Runtime 을 읽을 수 없기 때문이다 — Runtime 은 호스트 전용이라
	// 게임 DLL 안에서는 채워지지 않는다. 0 이면 경계를 낼 수 없다(아직 해석 전).
	float            CachedPixelsPerUnit = 0.0f;

protected:
	// 실제로 그려지는 쿼드와 같은 영역을 낸다 — 프레임 픽셀 크기 ÷ 유효 PPU × Size,
	// Offset 을 중심으로. 자산이 아직 해석되지 않았으면 빈 Rect(경계 미상)다.
	void ComputeLocalBounds(Rect& outBounds) const override;

private:
	friend void RegisterBuiltinComponents(CReflectionRegistry&);
	// 자산 캐시(SpriteAssetCache / CachedPixelsPerUnit / CachedPixelGeneration)를 갱신한 뒤
	// 경계 캐시를 무효화하는 통로. 이 캐시들은 세터가 아니라 렌더 시스템이 직접 채운다.
	friend class CSpriteRenderSystem;
	void NotifyAssetCacheChanged() { MarkBoundsDirty(); }

	AssetGuid m_spriteGuid = INVALID_ASSET_GUID;
	Vector2 m_size = Vector2(1.0f, 1.0f);
	Vector2 m_offset = Vector2(0.0f, 0.0f);
	std::uint32_t m_frameIndex = 0;
};
