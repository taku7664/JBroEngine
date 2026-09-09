#include <JBro/Framework2D/Framework2D.h>

#include <JBro/Graphics/Renderer.h>
#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Runtime/SystemContext.h>
#include "Rendering/RenderBridge2D.h"

#include <cmath>
#include <new>

namespace JBro
{
    Framework2D::~Framework2D()
    {
        Shutdown();
    }

    bool Framework2D::Initialize(const FrameworkContext& context)
    {
        if (m_initialized || false == std::isfinite(context.fixedDeltaTime)
            || context.fixedDeltaTime <= 0.0f || context.maxFixedStepsPerFrame == 0
            || (context.renderer != nullptr && false == context.renderer->IsInitialized()))
        {
            return false;
        }
        m_context = context;
        JAllocator allocator = context.memory.persistent;
        if (allocator.allocate == nullptr || allocator.free == nullptr)
        {
            allocator = CreateDefaultAllocator();
        }
        try
        {
            const auto capacity = context.renderer != nullptr ? context.renderer->GetSpriteSubmissionLimit() : 0;
            if (false == m_renderWorld.ReserveSprites(capacity))
            {
                Shutdown();
                return false;
            }
            m_canvas = MakeOwnerPtr<Canvas>(allocator);
            CreateDefaultSystems();
            m_canvas->GetSystems().Initialize(*m_canvas);
        }
        catch (const std::bad_alloc&)
        {
            Shutdown();
            return false;
        }
        catch (...)
        {
            Shutdown();
            throw;
        }
        m_initialized = true;
        return true;
    }

    bool Framework2D::BindScriptContexts() noexcept
    {
        if (false == m_initialized)
        {
            return false;
        }
        auto* physics = m_canvas->GetSystems().FindSystem<System::Physics2DSystem>();
        if (physics == nullptr)
        {
            return false;
        }
        auto systems = GetSystemContext();
        systems.Physics2D = physics;
        BindSystemContext(systems);
        BindFramework2DServiceContext({});
        return true;
    }

    void Framework2D::UnbindScriptContexts() noexcept
    {
        if (m_canvas.Get() == nullptr)
        {
            return;
        }
        auto systems = GetSystemContext();
        auto* physics = m_canvas->GetSystems().FindSystem<System::Physics2DSystem>();
        if (physics != nullptr && systems.Physics2D == physics)
        {
            systems.Physics2D = nullptr;
            BindSystemContext(systems);
            BindFramework2DServiceContext({});
        }
    }

    void Framework2D::Update(float deltaTime)
    {
        if (false == m_initialized)
        {
            return;
        }
        m_renderWorld.BeginFrame();
        if (false == std::isfinite(deltaTime) || deltaTime < 0.0f)
        {
            return;
        }
        m_canvas->BeginFrame();
        RunFixedSteps(deltaTime);
        m_canvas->GetSystems().Update(*m_canvas, deltaTime);
        m_renderWorld.EndFrame();
    }

    bool Framework2D::Render()
    {
        if (false == m_initialized || m_context.renderer == nullptr)
        {
            return false;
        }
        return Internal::SubmitRenderWorld2D(m_renderWorld, *m_context.renderer);
    }

    void Framework2D::Shutdown()
    {
        UnbindScriptContexts();
        // Canvas shuts down its systems before objects/components and render storage disappear.
        m_canvas.Reset();
        m_renderWorld = {};
        m_context     = {};
        m_fixedAccumulator = 0.0;
        m_initialized = false;
    }

    Canvas* Framework2D::GetCanvas()
    {
        return m_canvas.Get();
    }

    RenderWorld2D* Framework2D::GetRenderWorld()
    {
        return &m_renderWorld;
    }

    void Framework2D::CreateDefaultSystems()
    {
        auto& systems = m_canvas->GetSystems();
        systems.AddSystem<System::Transform2DSystem>();
        systems.AddSystem<System::Physics2DSystem>();
        systems.AddSystem<System::Camera2DSystem>().SetRenderWorld(&m_renderWorld);
        systems.AddSystem<System::SpriteRender2DSystem>().SetRenderWorld(&m_renderWorld);
    }
    void Framework2D::RunFixedSteps(float deltaTime)
    {
        m_fixedAccumulator += deltaTime;
        std::uint32_t steps = 0;
        while (m_fixedAccumulator >= m_context.fixedDeltaTime && steps < m_context.maxFixedStepsPerFrame)
        {
            m_canvas->GetSystems().FixedUpdate(*m_canvas, m_context.fixedDeltaTime);
            m_fixedAccumulator -= m_context.fixedDeltaTime;
            ++steps;
        }
        if (m_fixedAccumulator >= m_context.fixedDeltaTime)
        {
            // Drop excess whole steps after a long stall; preserve the fractional step.
            m_fixedAccumulator = std::fmod(m_fixedAccumulator, m_context.fixedDeltaTime);
        }
    }

    IFramework* CreateFramework2D()
    {
        return new Framework2D();
    }

    void DestroyFramework2D(IFramework* framework)
    {
        delete framework;
    }
}
