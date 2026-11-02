#include <JBro/Framework3DSystem/System/MeshRender3DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
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
            // 해석 패스. 저장되는 것은 아이디고 핸들은 이번 실행의 자리다 - 비어 있으면 표에서 채운다.
            if (renderer.mesh.generation == 0 && false == renderer.meshId.IsNull() && m_library != nullptr)
            {
                renderer.mesh = m_library->Resolve(renderer.meshId);
            }
            if (renderer.mesh.generation == 0)
            {
                return;
            }
            // 메시 에셋이면 렌더러 메시로 푼다(D-365). 빌트인은 이미 렌더러 핸들이다. 아직 못 올렸으면 이번 프레임은 건너뛴다.
            const AssetHandle gpuMesh = m_library != nullptr ? m_library->ResolveHandle(renderer.mesh) : renderer.mesh;
            if (gpuMesh.generation == 0)
            {
                return;
            }
            MeshRenderItem item;
            item.owner = owner;
            item.position = world->worldPosition;
            item.rotation = world->worldRotation;
            item.scale = world->worldScale;
            item.mesh = gpuMesh;
            // `material` 은 에셋 핸들이다. 렌더러에 가는 것은 라이브러리가 지은 렌더러 머티리얼이다(스프라이트와 같다).
            item.material = m_materialLibrary != nullptr ? m_materialLibrary->Resolve(renderer.material) : AssetHandle{};
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
