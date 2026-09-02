#include <JBro/Framework2D/Framework2D.h>

#include <JBro/Core/ECS/ComponentRegistry.h>
#include <JBro/Runtime/World.h>

namespace JBro::Engine
{
    bool Framework2D::Initialize(const FrameworkContext& context)
    {
        if (m_initialized) return false;
        m_context = context;
        JAllocator allocator = context.memory.persistent;
        if (allocator.allocate == nullptr || allocator.free == nullptr) allocator = CreateDefaultAllocator();
        m_canvas = MakeOwnerPtr<CCanvas>(allocator);
        m_world = &m_canvas->GetWorld();
        m_world->RegisterComponent<Transform2DComponent>(Transform2DType);
        m_world->RegisterComponent<WorldTransform2DComponent>(WorldTransform2DType);
        m_world->RegisterComponent<Camera2DComponent>(Camera2DType);
        m_world->RegisterComponent<SpriteRenderer2DComponent>(SpriteRenderer2DType);
        m_world->RegisterComponent<Rigidbody2DComponent>(Rigidbody2DType);
        m_world->RegisterComponent<Collider2DComponent>(Collider2DType);
        m_canvas->CreateLayer("Default");
        m_initialized = true;
        return true;
    }

    void Framework2D::Update(float)
    {
        if (false == m_initialized || m_world == nullptr) return;
        m_world->FlushCommands();
        m_canvas->PruneDeadEntities();
    }

    void Framework2D::Shutdown()
    {
        m_canvas.reset();
        m_world = nullptr;
        m_context = {};
        m_initialized = false;
    }

    void Framework2D::RegisterComponents(ComponentRegistry& registry)
    {
        registry.Register({ Transform2DType, sizeof(Transform2DComponent), alignof(Transform2DComponent) });
        registry.Register({ WorldTransform2DType, sizeof(WorldTransform2DComponent), alignof(WorldTransform2DComponent) });
        registry.Register({ Camera2DType, sizeof(Camera2DComponent), alignof(Camera2DComponent) });
        registry.Register({ SpriteRenderer2DType, sizeof(SpriteRenderer2DComponent), alignof(SpriteRenderer2DComponent) });
        registry.Register({ Rigidbody2DType, sizeof(Rigidbody2DComponent), alignof(Rigidbody2DComponent) });
        registry.Register({ Collider2DType, sizeof(Collider2DComponent), alignof(Collider2DComponent) });
    }

    CWorld* Framework2D::GetWorld() { return m_world; }
    CCanvas* Framework2D::GetCanvas() { return m_canvas.get(); }
    RenderWorld2D* Framework2D::GetRenderWorld() { return &m_renderWorld; }

    void Framework2D::CreateDefaultSystems() {}
    void Framework2D::RunFixedSteps(float) {}
    void Framework2D::ExtractRenderWorld() {}

    IFramework* CreateFramework2D() { return new Framework2D(); }
    void DestroyFramework2D(IFramework* framework) { delete framework; }
}
