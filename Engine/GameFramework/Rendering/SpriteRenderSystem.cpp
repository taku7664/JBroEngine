#include "pch.h"
#include "SpriteRenderSystem.h"

#include "Core/Asset/IAssetManager.h"
#include "Core/Asset/SpriteAsset.h"
#include "Core/Asset/TextureAsset.h"
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
			if ((false == mesh.IsValid() || false == material.IsValid()) && m_assetManager && m_rhiDevice && forwardRenderer)
			{
				SafePtr<IAsset> asset = m_assetManager->LoadAsset(sprite.SpriteGuid);
				CTextureAsset* textureAsset = nullptr;
				if (asset && EAssetType::Texture == asset->GetAssetType())
				{
					textureAsset = static_cast<CTextureAsset*>(asset.TryGet());
				}
				else if (asset && EAssetType::Sprite == asset->GetAssetType())
				{
					CSpriteAsset* spriteAsset = static_cast<CSpriteAsset*>(asset.TryGet());
					if (spriteAsset)
					{
						SafePtr<IAsset> texture = m_assetManager->LoadAsset(spriteAsset->TextureGuid);
						if (texture && EAssetType::Texture == texture->GetAssetType())
						{
							textureAsset = static_cast<CTextureAsset*>(texture.TryGet());
						}
					}
				}

				if (textureAsset && textureAsset->EnsureGpuTexture(*m_rhiDevice))
				{
					mesh = forwardRenderer->GetQuadMesh();
					OwnerPtr<CRenderMaterial> generatedMaterial = MakeOwnerPtr<CRenderMaterial>(
						forwardRenderer->GetSpritePipeline(),
						textureAsset->GetGpuTexture(),
						forwardRenderer->GetDefaultSampler(),
						ERenderQueue::Transparent);
					material = generatedMaterial.GetSafePtr();
					sprite.Mesh = mesh;
					sprite.Material = material;
					sprite.RuntimeMaterial = std::move(generatedMaterial);
				}
			}

			if (false == mesh.IsValid() || false == material.IsValid())
			{
				return;
			}

			RenderItem item;
			item.Mesh = mesh;
			item.Material = material;
			item.Queue = material->GetRenderQueue();
			item.LayerMask = sprite.LayerMask;
			const Matrix3x2 spriteLocalTransform = Matrix3x2::Transform(sprite.Offset, 0.0f, sprite.Size);
			item.Transform = spriteLocalTransform * GetWorldTransform(scene, entity);
			for (int i = 0; i < 4; ++i)
			{
				item.Color[i] = sprite.Color[i];
			}
			item.SortOrder = sprite.SortOrder;
			m_renderScene->Submit(item);
		});
}
