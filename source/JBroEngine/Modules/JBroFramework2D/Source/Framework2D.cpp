#include <JBro/Framework2D/Framework2D.h>

namespace JBro
{
    bool Framework2D::Initialize(const FrameworkContext& context)
    {
        if (m_initialized) return false;
        m_context = context;
        JAllocator allocator = context.memory.persistent;
        if (allocator.allocate == nullptr || allocator.free == nullptr) allocator = CreateDefaultAllocator();
        m_canvas = MakeOwnerPtr<Canvas>(allocator);
        CreateDefaultSystems();
        m_systems.Initialize(*m_canvas);
        m_initialized = true;
        return true;
    }

    void Framework2D::Update(float deltaTime)
    {
        if (false == m_initialized) return;
        RunFixedSteps(deltaTime);
        m_systems.Update(*m_canvas, deltaTime);
    }

    void Framework2D::Shutdown()
    {
        if (m_canvas)
        {
            m_systems.Shutdown(*m_canvas);
            m_systems.RemoveAllSystems(*m_canvas);
        }
        m_canvas.Reset();
        m_context     = {};
        m_initialized = false;
    }

    Canvas*        Framework2D::GetCanvas()      { return m_canvas.Get(); }
    RenderWorld2D* Framework2D::GetRenderWorld() { return &m_renderWorld; }

    void Framework2D::CreateDefaultSystems()
    {
        // 실제 시스템 등록은 F/G 워크트리에서 채운다.
    }
    void Framework2D::RunFixedSteps(float deltaTime)
    {
        m_fixedAccumulator += deltaTime;
        while (m_fixedAccumulator >= m_fixedDeltaTime)
        {
            m_systems.FixedUpdate(*m_canvas, m_fixedDeltaTime);
            m_fixedAccumulator -= m_fixedDeltaTime;
        }
    }

    IFramework* CreateFramework2D()                       { return new Framework2D(); }
    void        DestroyFramework2D(IFramework* framework) { delete framework; }
}
