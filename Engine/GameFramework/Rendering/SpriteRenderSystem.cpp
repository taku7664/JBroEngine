#include "pch.h"
#include "SpriteRenderSystem.h"

#include "Core/Asset/IAssetManager.h"
#include "Core/Asset/SpriteAsset.h"
#include "Core/EngineCore.h"
#include "Core/Renderer/Forward2DRenderer.h"
#include "Core/Renderer/IRenderMaterial.h"
#include "Core/Renderer/IRenderMesh.h"
#include "Core/Renderer/IRenderScene.h"
#include "Core/Renderer/RenderResources2D.h"
#include "Core/Renderer/RendererTypes.h"
#include "Core/RHI/IRHIDevice.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Component/TransformHierarchy2D.h"
#include "GameFramework/Component/GameObject.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneTransformUtils.h"

CSpriteRenderSystem::CSpriteRenderSystem(IRenderScene* renderScene)
	: m_renderScene(renderScene)
{
}

void CSpriteRenderSystem::SetRenderScene(IRenderScene* renderScene)
{
	m_renderScene = renderScene;
}

void CSpriteRenderSystem::SetDependencies(IAssetManager* assetManager, IRHIDevice* rhiDevice, IRenderer* renderer)
{
	m_assetManager = assetManager;
	m_rhiDevice = rhiDevice;
	m_renderer = renderer;
}

IRenderScene* CSpriteRenderSystem::GetRenderScene() const
{
	return m_renderScene;
}

void CSpriteRenderSystem::ClearMaterialCache()
{
	m_materialCache.clear();
}

void CSpriteRenderSystem::OnUpdate(CScene& scene)
{
	if (nullptr == m_renderScene)
	{
		return;
	}

	scene.ForEach<GameObject, Transform2D, SpriteRenderer2D>(
		[this, &scene](EntityId entity, const GameObject& gameObject, const Transform2D&, SpriteRenderer2D& sprite)
		{
			if (false == gameObject.IsActive || false == sprite.IsEnabled)
			{
				return;
			}

			SafePtr<IRenderMesh> mesh = sprite.Mesh;
			SafePtr<IRenderMaterial> material = sprite.Material;
			CForward2DRenderer* forwardRenderer = dynamic_cast<CForward2DRenderer*>(m_renderer);

			// SpriteGuid 는 CSpriteAsset 을 가리킨다 (이전 CTextureAsset 통합됨).
			// PPU 기반 자연 크기 계산을 위해 매 프레임 자산을 fetch — AssetManager 는 캐시 hit 처리.
			CSpriteAsset* spriteAsset = nullptr;
			if (m_assetManager)
			{
				SafePtr<IAsset> asset = m_assetManager->LoadAsset(sprite.SpriteGuid);
				if (asset && EAssetType::Sprite == asset->GetAssetType())
				{
					spriteAsset = static_cast<CSpriteAsset*>(asset.TryGet());
				}
			}

			if ((false == mesh.IsValid() || false == material.IsValid()) && m_rhiDevice && forwardRenderer)
			{
				if (spriteAsset && spriteAsset->EnsureGpuTexture(*m_rhiDevice))
				{
					mesh = forwardRenderer->GetQuadMesh();
					OwnerPtr<CRenderMaterial> generatedMaterial = MakeOwnerPtr<CRenderMaterial>(
						forwardRenderer->GetSpritePipeline(),
						spriteAsset->GetGpuTexture(),
						forwardRenderer->GetDefaultSampler(),
						ERenderQueue::Transparent);
					material = generatedMaterial.GetSafePtr();
					sprite.Mesh = mesh;
					sprite.Material = material;
					m_materialCache[entity] = std::move(generatedMaterial);
				}
			}

			if (false == mesh.IsValid() || false == material.IsValid())
			{
				return;
			}

			// 월드 크기 = (픽셀 크기 / 유효 PPU) × sprite.Size (스케일 배수).
			// 자산 PPU 가 0 이면 EngineCore 폴백(프로젝트 Default PPU) 사용.
			Vector2 finalSize = sprite.Size;
			if (spriteAsset)
			{
				const float effectivePPU = spriteAsset->GetEffectivePixelsPerUnit(Engine.PixelsPerUnit);
				const float pxW = static_cast<float>(spriteAsset->GetWidth());
				const float pxH = static_cast<float>(spriteAsset->GetHeight());
				finalSize.x = (pxW / effectivePPU) * sprite.Size.x;
				finalSize.y = (pxH / effectivePPU) * sprite.Size.y;
			}

			RenderItem item;
			item.Mesh = mesh;
			item.Material = material;
			item.Queue = material->GetRenderQueue();
			item.LayerMask = sprite.LayerMask;
			const Matrix3x2 spriteLocalTransform = Matrix3x2::Transform(sprite.Offset, 0.0f, finalSize);
			item.Transform = spriteLocalTransform * GetWorldTransform(scene, entity);
			for (int i = 0; i < 4; ++i)
			{
				item.Color[i] = sprite.Color[i];
			}
			item.SortOrder = sprite.SortOrder;
			item.Entity    = entity;
			m_renderScene->Submit(item);
		});
}
