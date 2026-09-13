#include <JBro/Framework3DSystem/Framework3D.h>

#include <JBro/Framework3D/BuiltinComponentProperties3D.h>
#include <JBro/Graphics/Renderer.h>

#include <cmath>
#include <new>

namespace JBro
{
    Framework3D::~Framework3D()
    {
        Shutdown();
    }

    bool Framework3D::Initialize(const FrameworkContext& context)
    {
        if (m_initialized
            || false == std::isfinite(context.fixedDeltaTime)
            || context.fixedDeltaTime <= 0.0f
            || context.maxFixedStepsPerFrame == 0
            || (context.renderer != nullptr && false == context.renderer->IsInitialized()))
        {
            return false;
        }

        // 빌트인 컴포넌트가 자기 프로퍼티를 이름으로 내놓을 수 있게 한다.
        // 이름표(NameTable)를 쓰므로 캔버스보다 먼저, 프레임이 돌기 전에 해 둔다.
        Component::RegisterBuiltinComponentProperties3D();
        JAllocator allocator = context.memory.persistent;
        if (allocator.allocate == nullptr || allocator.free == nullptr)
        {
            allocator = CreateDefaultAllocator();
        }

        try
        {
            m_canvas = MakeOwnerPtr<Canvas>(allocator);
            m_canvas->GetSystems().Initialize(*m_canvas);
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }

        m_context = context;
        m_initialized = true;
        return true;
    }

    bool Framework3D::BindScriptContexts() noexcept
    {
        return m_initialized;
    }

    void Framework3D::UnbindScriptContexts() noexcept
    {
    }

    void Framework3D::Update(float deltaTime)
    {
        if (false == m_initialized
            || false == std::isfinite(deltaTime)
            || deltaTime < 0.0f)
        {
            return;
        }

        m_canvas->BeginFrame();
        RunFixedSteps(deltaTime);
        m_canvas->GetSystems().Update(*m_canvas, deltaTime);
        m_canvas->FlushPendingDestroy();
    }

    RenderResult Framework3D::Render()
    {
        // The 3D backend is a declared extension point, not a functioning renderer yet.
        // 제출할 것이 없는 것과 실패는 다르다. 호스트는 계속 돌아야 한다(D-49).
        return RenderResult::NothingToSubmit;
    }

    void Framework3D::Shutdown()
    {
        m_canvas.Reset();
        m_context = {};
        m_fixedAccumulator = 0.0;
        m_initialized = false;
    }

    Canvas* Framework3D::GetCanvas()
    {
        return m_canvas.Get();
    }

    void Framework3D::RunFixedSteps(float deltaTime)
    {
        m_fixedAccumulator += deltaTime;
        std::uint32_t steps = 0;
        while (m_fixedAccumulator >= m_context.fixedDeltaTime
            && steps < m_context.maxFixedStepsPerFrame)
        {
            m_canvas->GetSystems().FixedUpdate(*m_canvas, m_context.fixedDeltaTime);
            // 고정 스텝 묶음의 각 스텝 뒤가 첫 안전 지점이다(D-45).
            m_canvas->FlushPendingDestroy();
            m_fixedAccumulator -= m_context.fixedDeltaTime;
            ++steps;
        }
        if (m_fixedAccumulator >= m_context.fixedDeltaTime)
        {
            m_fixedAccumulator = std::fmod(m_fixedAccumulator, m_context.fixedDeltaTime);
        }
    }

    IFramework* CreateFramework3D()
    {
        return new Framework3D();
    }

    void DestroyFramework3D(IFramework* framework)
    {
        delete framework;
    }
}
