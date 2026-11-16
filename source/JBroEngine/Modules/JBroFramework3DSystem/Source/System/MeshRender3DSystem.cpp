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

    void MeshRender3DSystem::SetMeshLibrary(const MeshLibrary* library)
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
            // 해석 패스. 저장되는 것은 아이디고 핸들은 이번 실행의 자리다 - 비어 있으면 표에서 채운다.
            if (renderer.mesh.generation == 0 && false == renderer.meshId.IsNull() && m_library != nullptr)
            {
                renderer.mesh = m_library->Resolve(renderer.meshId);
            }
            // **풀리지 않으면 정육면체다**(D-335 우선순위 2). 2D 의 `SpriteRenderer2D` 가 스프라이트가 비었거나 풀리지 않으면 흰 사각형을
            // 그리는 것과 같다 - `컴포넌트 추가` 로 붙이자마자 보이고, 메시 칸에서 다른 도형·에셋을 고른다. 컴포넌트의 핸들에는 넣지 않는다:
            // 넣으면 다음 Update 의 해석 패스가 채워진 핸들을 보고 진짜 메시를 다시 찾지 않는다.
            AssetHandle mesh = renderer.mesh;
            if (mesh.generation == 0 && m_library != nullptr)
            {
                mesh = m_library->Resolve(MeshLibrary::BuiltinCubeId());
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
