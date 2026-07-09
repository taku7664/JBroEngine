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
#include "GameFramework/Scene/Scene.h"

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
	m_renderer = renderer;
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

void CSpriteRenderSystem::OnUpdate(CGameScene& scene)
{
	if (nullptr == m_renderScene)
	{
		return;
	}

	CForward2DRenderer* forwardRenderer = dynamic_cast<CForward2DRenderer*>(m_renderer);

	scene.ForEach<SpriteRenderer2D>(
		[this, forwardRenderer](SpriteRenderer2D& sprite)
		{
			CGameObject* owner = sprite.GetOwner();
			if (false == IsActiveComponent(sprite))
			{
				return;
			}
			const void* cacheKey = &sprite;

			SafePtr<IRenderMesh> mesh = sprite.Mesh;
			SafePtr<IRenderMaterial> material = sprite.Material;

			// SpriteGuid 는 CSpriteAsset 을 가리킨다 (이전 CTextureAsset 통합됨).
			// 자산 캐시 — SpriteGuid 가 바뀌었거나 캐시가 죽었을 때만 LoadAsset 호출.
			// AssetRef 가 strong 이라 캐시가 살아있는 동안 자산이 unload 되지 않음.
			if (sprite.CachedSpriteGuid != sprite.SpriteGuid || false == sprite.SpriteAssetCache.IsValid())
			{
				if (m_assetManager) sprite.SpriteAssetCache = m_assetManager->LoadAsset(sprite.SpriteGuid);
				else                sprite.SpriteAssetCache.Reset();
				sprite.CachedSpriteGuid = sprite.SpriteGuid;
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
					m_materialCache[cacheKey] = std::move(generatedMaterial);
				}
			}

			if (false == mesh.IsValid() || false == material.IsValid())
			{
				return;
			}

			// 월드 크기 = (픽셀 크기 / 유효 PPU) × sprite.Size (스케일 배수).
			// 자산 PPU 가 0 이면 ScriptCore 폴백(프로젝트 Default PPU) 사용.
			Vector2 finalSize = sprite.Size;
			float uvRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
			if (spriteAsset)
			{
				const float effectivePPU = spriteAsset->GetEffectivePixelsPerUnit(m_pixelsPerUnit);
				const float texW = static_cast<float>(spriteAsset->GetWidth());
				const float texH = static_cast<float>(spriteAsset->GetHeight());

				// 프레임 선택 — 시트 슬라이스가 있으면 FrameIndex 프레임, 없으면 전체(항등 UV).
				float frameW = texW;
				float frameH = texH;
				const std::vector<SpriteFrame>& frames = spriteAsset->GetFrames();
				if (false == frames.empty() && texW > 0.0f && texH > 0.0f)
				{
					const std::uint32_t count = static_cast<std::uint32_t>(frames.size());
					const std::uint32_t idx = sprite.FrameIndex < count ? sprite.FrameIndex : count - 1;
					const SpriteFrame& frame = frames[idx];
					frameW = static_cast<float>(frame.Width);
					frameH = static_cast<float>(frame.Height);
					uvRect[0] = static_cast<float>(frame.X) / texW;
					uvRect[1] = static_cast<float>(frame.Y) / texH;
					uvRect[2] = frameW / texW;
					uvRect[3] = frameH / texH;
				}

				finalSize.x = (frameW / effectivePPU) * sprite.Size.x;
				finalSize.y = (frameH / effectivePPU) * sprite.Size.y;
			}

			// 반전 — 월드 크기 부호를 뒤집으면 쿼드가 미러링된다(음수 스케일).
			if (sprite.FlipX) finalSize.x = -finalSize.x;
			if (sprite.FlipY) finalSize.y = -finalSize.y;

			RenderItem item;
			item.Mesh = mesh;
			item.Material = material;
			item.Pipeline = material->GetPipeline();
			item.Texture = material->GetTexture();
			item.Sampler = material->GetSampler();
			item.Queue = material->GetRenderQueue();
			item.LayerMask = sprite.LayerMask;
			const Matrix3x2 spriteLocalTransform = Matrix3x2::Transform(sprite.Offset, 0.0f, finalSize);
			item.Transform = spriteLocalTransform * owner->GetWorld().Matrix;
			for (int i = 0; i < 4; ++i)
			{
				item.Color[i] = sprite.Color[i];
				item.UvRect[i] = uvRect[i];
			}
			item.SortOrder = sprite.SortOrder;
			item.Entity    = owner; // 불투명 키(주소). 렌더러는 집합 비교만, 역참조 안 함.
			m_renderScene->Submit(item);
		});
}
