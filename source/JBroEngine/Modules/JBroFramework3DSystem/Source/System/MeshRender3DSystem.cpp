#include <JBro/Framework3DSystem/System/MeshRender3DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>
#include <JBro/Framework3DSystem/Rendering/RenderWorld3D.h>
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
            if (layer != nullptr && false == layer->IsVisible())
            {
                return;
            }
            const auto* world = canvas.FindComponentRaw<Component::Transform3D>(owner);
            if (world == nullptr || false == world->IsActiveComponent() || false == world->worldValid)
            {
                return;
            }
            // **컴포넌트의 `mesh` 는 에셋 시스템의 핸들이다**(D-342) - 해석 패스(`BindCanvasAssets`)가 `meshId` 로 채운다. 이 시스템은 그것을 되쓰지 않고
            // 라이브러리로 렌더러 메시를 푼다(2D 의 스프라이트와 같다). 모델의 메시가 아니면(빌트인 도형은 레지스트리에 없어 핸들이 빈다) 아이디로 빌트인 표를 본다.
            AssetHandle mesh;
            if (m_library != nullptr)
            {
                mesh = m_library->ResolveAsset(renderer.mesh);
                if (mesh.generation == 0 && false == renderer.meshId.IsNull())
                {
                    mesh = m_library->Resolve(renderer.meshId);
                }
                // **풀리지 않으면 정육면체다**(D-337). 2D 의 `SpriteRenderer2D` 가 스프라이트가 비었거나 풀리지 않으면 흰 사각형을 그리는 것과 같다.
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
            item.material = renderer.material;
            item.tint = renderer.tint;
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
