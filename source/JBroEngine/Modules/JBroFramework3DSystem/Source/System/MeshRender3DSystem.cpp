#include <JBro/Framework3DSystem/System/MeshRender3DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>
#include <JBro/Framework3DSystem/Rendering/RenderWorld3D.h>
#include <JBro/Runtime/GameObject.h>

namespace JBro::System
{
    int MeshRender3DSystem::GetExecutionOrder() const
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
            GameObject* owner = Internal::CanvasAccess::GetOwner(renderer);
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
            if (renderer.mesh.generation == 0 && renderer.meshId.value != 0 && m_library != nullptr)
            {
                renderer.mesh = m_library->Resolve(renderer.meshId);
            }
            if (renderer.mesh.generation == 0)
            {
                return;
            }
            MeshRenderItem item;
            item.owner = owner;
            item.position = world->worldPosition;
            item.rotation = world->worldRotation;
            item.scale = world->worldScale;
            item.mesh = renderer.mesh;
            item.material = renderer.material;
            item.tint = renderer.tint;
            m_renderWorld->SubmitMesh(item);
        });
    }

    void MeshRender3DSystem::OnUpdate(Canvas& canvas, float deltaTime)
    {
        (void)deltaTime;
        ExtractRenderWorld(canvas);
    }
}
