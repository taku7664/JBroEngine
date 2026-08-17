#include "pch.h"
#include "SpriteRenderSystem.h"

#include "Core/Asset/IAssetManager.h"
#include "Core/Asset/SpriteAsset.h"
#include "Core/Renderer/Forward2DRenderer.h"
#include "Core/Renderer/IRenderMaterial.h"
#include "Core/Renderer/IRenderMesh.h"
#include "Core/Renderer/IRenderResourceCache.h"
#include "Core/Renderer/IRenderScene.h"
#include "Core/Renderer/RenderResources2D.h"
#include "Core/Renderer/RendererTypes.h"
#include "Core/RHI/IRHIDevice.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Canvas/Canvas.h"
#include "Utillity/Types/FrameLiveness.h"

CSpriteRenderSystem::CSpriteRenderSystem(IRenderScene* renderScene)
	: m_renderScene(renderScene)
{
}

void CSpriteRenderSystem::SetRenderScene(IRenderScene* renderScene)
{
	m_renderScene = renderScene;
}

void CSpriteRenderSystem::SetDependencies(IAssetManager* assetManager, IRHIDevice* rhiDevice, IRenderer* renderer,
	IRenderResourceCache* renderResourceCache, float pixelsPerUnit)
{
	m_assetManager = assetManager;
	m_rhiDevice = rhiDevice;
	m_renderer = renderer ? renderer->AsForward2DRenderer() : nullptr;
	m_renderResourceCache = renderResourceCache;
	m_pixelsPerUnit = pixelsPerUnit;
}

IRenderScene* CSpriteRenderSystem::GetRenderScene() const
{
	return m_renderScene;
}

void CSpriteRenderSystem::ClearMaterialCache()
{
	m_materialCache.clear();
}

void CSpriteRenderSystem::OnUpdate(CGameCanvas& canvas)
{
	if (nullptr == m_renderScene)
	{
		return;
	}

	CForward2DRenderer* forwardRenderer = m_renderer;

	// 이번 프레임 스탬프 — 살아있는 캐시 엔트리에 찍고, 순회가 끝나면 뒤처진 엔트리를 지운다
	// (파괴/비활성 스프라이트의 머티리얼이 GPU 텍스처를 물고 무한 누적되는 것을 막는다. Shape/Text 와 동일).
	const std::uint64_t stamp = ++m_frameStamp;

	canvas.ForEach<SpriteRenderer2D>(
		[this, forwardRenderer, stamp](SpriteRenderer2D& sprite)
		{
			CGameObject* owner = sprite.GetOwner().TryGet();
			if (false == IsActiveComponent(sprite))
			{
				return;
			}
			const void* cacheKey = &sprite;
			// 아직 캐시 엔트리가 없으면 표시할 대상도 없다 — 아래에서 생성될 때 현재
			// 스탬프를 달고 들어간다. 엔트리가 없는 키를 "봤다"고 기록해 봐야 청소 결과는 같다.
			if (const auto live = m_materialCache.find(cacheKey); live != m_materialCache.end())
			{
				live->second.LastSeenFrame = stamp;
			}

			SafePtr<IRenderMesh> mesh = sprite.Mesh;
			SafePtr<IRenderMaterial> material = sprite.Material;

			// SpriteGuid 는 CSpriteAsset 을 가리킨다 (이전 CTextureAsset 통합됨).
			// 자산 캐시 — SpriteGuid 가 바뀌었거나 캐시가 죽었을 때만 LoadAsset 호출.
			// AssetRef 가 strong 이라 캐시가 살아있는 동안 자산이 unload 되지 않음.
			if (sprite.CachedSpriteGuid != sprite.GetSpriteGuid() || false == sprite.SpriteAssetCache.IsValid())
			{
				if (m_assetManager) sprite.SpriteAssetCache = m_assetManager->LoadAsset(sprite.GetSpriteGuid());
				else                sprite.SpriteAssetCache.Reset();
				sprite.CachedSpriteGuid = sprite.GetSpriteGuid();
				// 자산이 바뀌면 프레임 픽셀 크기가 달라진다 → 경계 캐시 무효화.
				sprite.NotifyAssetCacheChanged();
				// guid 가 바뀌면 옛 Mesh/Material 무효 — 옛 텍스처로 계속 그리지 않도록 비움.
				// (SpriteGuid = INVALID 로 해서 sprite 를 제거한 경우도 여기로 들어와 화면에서 사라진다.)
				mesh.Reset();
				material.Reset();
				sprite.Mesh.Reset();
				sprite.Material.Reset();
				m_materialCache.erase(cacheKey);
				sprite.CachedPixelGeneration = 0;
			}

			CSpriteAsset* spriteAsset = nullptr;
			if (sprite.SpriteAssetCache.IsValid() && EAssetType::Sprite == sprite.SpriteAssetCache->GetAssetType())
			{
				spriteAsset = static_cast<CSpriteAsset*>(sprite.SpriteAssetCache.Get());
			}

			// 유효 PPU 를 컴포넌트에 주입한다 — 컴포넌트는 전역 Runtime 을 읽을 수 없으므로
			// (호스트 전용) 이 값 없이는 스스로 경계를 낼 수 없다. 아래 GPU 리소스 분기에서
			// 조기 반환할 수 있으니 그보다 먼저 채운다.
			const float resolvedPixelsPerUnit = spriteAsset
				? spriteAsset->GetEffectivePixelsPerUnit(m_pixelsPerUnit)
				: 0.0f;
			if (sprite.CachedPixelsPerUnit != resolvedPixelsPerUnit)
			{
				sprite.CachedPixelsPerUnit = resolvedPixelsPerUnit;
				sprite.NotifyAssetCacheChanged();
			}

			// 자산 픽셀이 reload 되었으면 캐시된 Mesh/Material 의 텍스처 참조가 죽었을 수 있다.
			// pixelGeneration 이 증가하면 invalidate → 아래 분기에서 새 GPU 텍스처로 재생성.
			if (spriteAsset && spriteAsset->GetPixelGeneration() != sprite.CachedPixelGeneration)
			{
				mesh.Reset();
				material.Reset();
				sprite.Mesh.Reset();
				sprite.Material.Reset();
				m_materialCache.erase(cacheKey);
				sprite.CachedPixelGeneration = spriteAsset->GetPixelGeneration();
				// 픽셀 재로드로 텍스처 크기/슬라이스가 달라졌을 수 있다 → 경계 캐시 무효화.
				sprite.NotifyAssetCacheChanged();
			}

			if ((false == mesh.IsValid() || false == material.IsValid()) && m_rhiDevice && forwardRenderer)
			{
				SafePtr<IRHITexture> gpuTexture = nullptr;
				if (spriteAsset && nullptr != m_renderResourceCache)
				{
					gpuTexture = m_renderResourceCache->AcquireSpriteTexture(spriteAsset->GetGuid(), *spriteAsset);
				}
				if (gpuTexture.IsValid())
				{
					mesh = forwardRenderer->GetQuadMesh();
					OwnerPtr<CRenderMaterial> generatedMaterial = MakeOwnerPtr<CRenderMaterial>(
						forwardRenderer->GetSpritePipeline(),
						gpuTexture,
						forwardRenderer->GetDefaultSampler(),
						ERenderQueue::Transparent);
					material = generatedMaterial.GetSafePtr();
					sprite.Mesh = mesh;
					sprite.Material = material;
					m_materialCache[cacheKey] = CachedMaterial{ std::move(generatedMaterial), stamp };
				}
			}

			if (false == mesh.IsValid() || false == material.IsValid())
			{
				return;
			}

			// 프레임 선택 — 시트 슬라이스가 있으면 FrameIndex 프레임, 없으면 전체(항등 UV).
			float uvRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
			if (spriteAsset)
			{
				const float texW = static_cast<float>(spriteAsset->GetWidth());
				const float texH = static_cast<float>(spriteAsset->GetHeight());
				const std::vector<SpriteFrame>& frames = spriteAsset->GetFrames();
				if (false == frames.empty() && texW > 0.0f && texH > 0.0f)
				{
					const std::uint32_t count = static_cast<std::uint32_t>(frames.size());
					const std::uint32_t idx = sprite.GetFrameIndex() < count ? sprite.GetFrameIndex() : count - 1;
					const SpriteFrame& frame = frames[idx];
					uvRect[0] = static_cast<float>(frame.X) / texW;
					uvRect[1] = static_cast<float>(frame.Y) / texH;
					uvRect[2] = static_cast<float>(frame.Width) / texW;
					uvRect[3] = static_cast<float>(frame.Height) / texH;
				}
			}

			// 쿼드의 중심/크기는 **컴포넌트가 낸다**(피벗·반전·유효 PPU 반영).
			// 경계 계산과 에디터 피킹이 같은 함수를 쓰므로 그리는 자리와 집는 자리가 갈릴 수 없다.
			Vector2 quadCenter = sprite.GetOffset();
			Vector2 quadSize   = sprite.GetSize();
			if (false == sprite.TryGetLocalQuad(quadCenter, quadSize))
			{
				// 자산 미해석 폴백 — 크기 배수를 유닛으로 그대로 쓰고 반전만 반영한다.
				if (sprite.GetFlipX()) quadSize.x = -quadSize.x;
				if (sprite.GetFlipY()) quadSize.y = -quadSize.y;
			}

			RenderItem item;
			item.Mesh = mesh;
			item.Material = material;
			item.Pipeline = material->GetPipeline();
			item.Texture = material->GetTexture();
			item.Sampler = material->GetSampler();
			item.Queue = material->GetRenderQueue();
			item.LayerIndex = owner->GetLayerIndex();
			// 스프라이트는 LocalHalfExtents 를 건드리지 않는다 — 공유 쿼드가 ±0.5 이고 크기는
			// 이 변환의 스케일이 들고 있으므로 기본값 {0.5, 0.5} 가 이미 정확한 컬링 상자다.
			// (도형·텍스트는 스케일이 1 이고 메시가 유닛 크기라 경계를 따로 넘겨야 한다.)
			const Matrix3x2 spriteLocalTransform = Matrix3x2::Transform(quadCenter, 0.0f, quadSize);
			item.Transform = spriteLocalTransform * owner->GetWorld().Matrix;
			for (int i = 0; i < 4; ++i)
			{
				item.Color[i] = sprite.Color[i];
				item.UvRect[i] = uvRect[i];
			}
			item.SortOrder = sprite.SortOrder;
			item.Entity    = owner; // 불투명 키(주소). 렌더러는 집합 비교만, 역참조 안 함.
			item.CastShadow = sprite.CastShadow;
			m_renderScene->Submit(item);
		});

	// 이번 프레임에 보이지 않은(파괴/비활성/제거된) 스프라이트의 머티리얼 캐시를 정리한다.
	RemoveStaleEntries(m_materialCache, stamp,
		[](const CachedMaterial& cached) { return cached.LastSeenFrame; });
}
