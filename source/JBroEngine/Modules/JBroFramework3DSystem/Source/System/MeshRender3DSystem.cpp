#include <JBro/Framework3DSystem/System/MeshRender3DSystem.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework3D/Component/Animator3D.h>
#include <JBro/Framework3DSystem/System/Animation3DSystem.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Rendering/MeshMaterialLibrary.h>
#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>
#include <JBro/Framework3DSystem/Rendering/RenderWorld3D.h>
#include <JBro/Graphics/MaterialLibrary.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro::System
{
    Int32 MeshRender3DSystem::GetExecutionOrder() const
    {
        return 400;
    }

    void MeshRender3DSystem::SetRenderWorld(RenderWorld3D* renderWorld)
    {
        m_renderWorld = renderWorld;
    }

    void MeshRender3DSystem::SetMeshLibrary(MeshLibrary* library)
    {
        m_library = library;
    }

    void MeshRender3DSystem::SetMaterialLibrary(MaterialLibrary* library)
    {
        m_materialLibrary = library;
    }

    void MeshRender3DSystem::SetMeshMaterialLibrary(MeshMaterialLibrary* library)
    {
        m_meshMaterials = library;
    }

    void MeshRender3DSystem::SetAnimation(AssetSystem* assets, const Animation3DSystem* animation)
    {
        m_assets = assets;
        m_animation = animation;
    }

    void MeshRender3DSystem::ExtractRenderWorld(Canvas& canvas)
    {
        if (m_renderWorld == nullptr)
        {
            return;
        }
        canvas.ForEach<Component::MeshRenderer3D>([&](Component::MeshRenderer3D& renderer)
        {
            if (false == renderer.visible || false == renderer.IsActiveComponent())
            {
                return;
            }
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(renderer);
            const Layer* layer = owner != nullptr ? owner->GetLayer() : nullptr;
            // 화면 레이어는 화면 UI 의 것이다(D-362) - 3D 카메라로 그리지 않는다.
            if (layer != nullptr && (false == layer->IsVisible() || layer->GetSpace() == LayerSpace::Screen))
            {
                return;
            }
            const auto* world = canvas.FindComponentRaw<Component::Transform3D>(owner);
            if (world == nullptr || false == world->IsActiveComponent() || false == world->worldValid)
            {
                return;
            }
            // **컴포넌트의 `mesh` 는 에셋 시스템의 핸들이다**(D-395) - 해석 패스(`BindCanvasAssets`)가 `meshId` 로 채운다. 이 시스템은 그것을 되쓰지 않고
            // 라이브러리로 렌더러 메시를 푼다(2D 의 스프라이트와 같다). 모델의 메시가 아니면(빌트인 도형은 레지스트리에 없어 핸들이 빈다) 아이디로 빌트인 표를 본다.
            AssetHandle mesh;
            if (m_library != nullptr)
            {
                mesh = m_library->ResolveAsset(renderer.mesh);
                if (mesh.generation == 0 && false == renderer.meshId.IsNull())
                {
                    mesh = m_library->Resolve(renderer.meshId);
                }
                // **풀리지 않으면 정육면체다**(D-390). 2D 의 `SpriteRenderer2D` 가 스프라이트가 비었거나 풀리지 않으면 흰 사각형을 그리는 것과 같다.
                if (mesh.generation == 0)
                {
                    mesh = m_library->Resolve(MeshLibrary::BuiltinCubeId());
                }
            }
            if (mesh.generation == 0)
            {
                return;
            }
            MeshRenderItem item;
            item.owner = owner;
            item.position = world->worldPosition;
            item.rotation = world->worldRotation;
            item.scale = world->worldScale;
            item.mesh = mesh;
            // **머티리얼은 칸 → 메시의 재질 → 기본 차례다**(D-399 (3)). 칸이 셰이더 머티리얼(D-348)이면 그것으로 그리고, 표준 머티리얼이면 표준 라이브러리가 푼다.
            // 칸이 비면 모델의 메시를 고르기만 해도 파일의 재질이 입혀진다. 둘 다 없으면 빈 핸들이고 렌더러가 기본 표준 머티리얼로 그린다.
            item.material = m_materialLibrary != nullptr ? m_materialLibrary->Resolve(renderer.material, renderer.GetInstanceId()) : AssetHandle{};
            if (item.material.generation == 0 && m_meshMaterials != nullptr)
            {
                item.meshMaterial = m_meshMaterials->ResolveAsset(renderer.material);
                if (item.meshMaterial.generation == 0)
                {
                    item.meshMaterial = m_meshMaterials->ResolveMeshMaterial(renderer.mesh);
                }
            }
            // **스킨 메시**(D-458): 자기나 가장 가까운 조상의 애니메이터를 따른다. 스키닝 행렬은 모델 뿌리 기준이라 자리는 애니메이터 오브젝트의 것이다.
            const MeshData* meshData = m_assets != nullptr && m_animation != nullptr && renderer.mesh.generation != 0 ? m_assets->GetMesh(renderer.mesh) : nullptr;
            if (meshData != nullptr && false == meshData->skin.IsEmpty())
            {
                for (Object::GameObject* at = owner; at != nullptr; at = at->GetParent())
                {
                    const auto* animator = canvas.FindComponentRaw<Component::Animator3D>(at);
                    if (animator == nullptr || false == animator->IsActiveComponent())
                    {
                        continue;
                    }
                    const Array<Matrix4x4>* skin = m_animation->FindSkinMatrices(animator->GetInstanceId());
                    const auto* place = canvas.FindComponentRaw<Component::Transform3D>(at);
                    if (skin != nullptr && place != nullptr && place->worldValid)
                    {
                        item.skin = skin;
                        item.position = place->worldPosition;
                        item.rotation = place->worldRotation;
                        item.scale = place->worldScale;
                    }
                    break;
                }
            }
            item.tint = renderer.tint;
            item.castShadows = renderer.castShadows;
            item.receiveShadows = renderer.receiveShadows;
            item.layerOrder = layer != nullptr ? layer->GetOrder() : 0;
            item.layerBlend = layer != nullptr ? layer->GetBlend() : LayerBlend::Normal;
            item.layerOpacity = layer != nullptr ? layer->GetOpacity() : Float(1.0f);
            item.layerParallax = layer != nullptr ? layer->GetParallax() : Float(1.0f);
            m_renderWorld->SubmitMesh(item);
        });
    }

    void MeshRender3DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        (void)deltaTime;
        ExtractRenderWorld(canvas);
    }
}
