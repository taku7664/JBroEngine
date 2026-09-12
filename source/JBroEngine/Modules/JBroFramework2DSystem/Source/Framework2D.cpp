#include <JBro/Framework2DSystem/Framework2D.h>

#include <JBro/Graphics/Renderer.h>
#include <JBro/Framework2D/Internal/ScriptModuleContext.h>
#include <JBro/Framework2D/Internal/SystemContext.h>
#include <JBro/Framework2D/ServiceContext.h>
#include "Rendering/RenderBridge2D.h"

#include <cmath>
#include <new>
#include <utility>

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
        m_scriptSystems = {};
        m_scriptSystems.Physics2D = physics;
        m_scriptServices = {};
        BindFramework2DSystemContext(m_scriptSystems);
        BindFramework2DServiceContext(m_scriptServices);
        // 같은 값을 블록으로도 내어 준다. 호스트가 그대로 DLL 에 건넨다.
        m_scriptBlocks[0] = MakeFramework2DSystemContextBlock(m_scriptSystems);
        m_scriptBlocks[1] = MakeFramework2DServiceContextBlock(m_scriptServices);
        m_scriptBlockCount = 2;
        return true;
    }

    JArrayView<ScriptContextBlock> Framework2D::GetScriptContextBlocks() const noexcept
    {
        return {m_scriptBlocks, m_scriptBlockCount};
    }

    void Framework2D::UnbindScriptContexts() noexcept
    {
        m_scriptBlockCount = 0;
        if (m_canvas.Get() == nullptr)
        {
            return;
        }
        auto* physics = m_canvas->GetSystems().FindSystem<System::Physics2DSystem>();
        if (physics != nullptr && GetFramework2DSystems().Physics2D == physics)
        {
            BindFramework2DSystemContext({});
            BindFramework2DServiceContext({});
        }
    }

    void Framework2D::Update(float deltaTime)
    {
        // dt 검증이 BeginFrame 보다 앞선다. 뒤에 두면 무효한 dt 한 번이
        // 수집된 프레임을 비운 채로 남겨, 호스트가 빈 화면을 제시한다.
        if (false == m_initialized
            || false == std::isfinite(deltaTime)
            || deltaTime < 0.0f)
        {
            return;
        }
        m_renderWorld.BeginFrame();
        m_canvas->BeginFrame();
        RunFixedSteps(deltaTime);
        m_canvas->GetSystems().Update(*m_canvas, deltaTime);
        m_canvas->FlushPendingDestroy();
        m_renderWorld.EndFrame();
    }

    RenderResult Framework2D::Render()
    {
        if (false == m_initialized || m_context.renderer == nullptr)
        {
            return RenderResult::Failed;
        }
        return Internal::SubmitRenderWorld2D(m_renderWorld, *m_context.renderer);
    }

    void Framework2D::Shutdown()
    {
        UnbindScriptContexts();
        // Canvas shuts down its systems before objects/components and render storage disappear.
        m_layer2DStates.Clear();
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

    Layer* Framework2D::CreateLayer(const char* name)
    {
        if (false == m_initialized || m_canvas.Get() == nullptr)
        {
            return nullptr;
        }

        try
        {
            return &m_canvas->CreateLayer(name);
        }
        catch (const std::bad_alloc&)
        {
            return nullptr;
        }
    }

    bool Framework2D::DestroyLayer(LayerId layer)
    {
        if (false == m_initialized || m_canvas.Get() == nullptr)
        {
            return false;
        }
        if (false == m_canvas->DestroyLayer(layer))
        {
            return false;
        }
        m_layer2DStates.Remove(layer);
        return true;
    }

    bool Framework2D::MoveLayer(LayerId layer, std::size_t newIndex)
    {
        if (false == m_initialized || m_canvas.Get() == nullptr)
        {
            return false;
        }
        return m_canvas->MoveLayer(layer, newIndex);
    }

    // 2D 상태는 살아 있는 런타임 레이어의 함수다. 여기서 지연 생성하고 스테일 항목을 정리하므로,
    // Canvas 로 직접 만들거나 파괴한 레이어도 같은 불변식을 따른다. Runtime 공개 계약은 늘리지 않는다.
    // LayerId 는 단조 증가라 재사용이 없고, 남은 항목이 다른 레이어의 상태로 오인되지 않는다.
    Layer2D* Framework2D::GetLayer2D(LayerId layer)
    {
        if (m_canvas.Get() == nullptr)
        {
            return nullptr;
        }
        if (m_canvas->FindLayer(layer) == nullptr)
        {
            m_layer2DStates.Remove(layer);
            return nullptr;
        }

        if (OwnerPtr<Layer2D>* existing = m_layer2DStates.Find(layer))
        {
            return existing->Get();
        }

        try
        {
            if (false == m_layer2DStates.TryAdd(layer, MakeOwnerPtr<Layer2D>()))
            {
                return nullptr;
            }
        }
        catch (const std::bad_alloc&)
        {
            return nullptr;
        }
        OwnerPtr<Layer2D>* state = m_layer2DStates.Find(layer);
        return state == nullptr ? nullptr : state->Get();
    }

    void Framework2D::CreateDefaultSystems()
    {
        auto& systems = m_canvas->GetSystems();
        systems.AddSystem<System::Transform2DSystem>();
        // 변환 뒤, 렌더 추출 전이다. 실행 순서는 GetExecutionOrder 가 정한다.
        systems.AddSystem<System::ScriptSystem>();
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
            // 고정 스텝 묶음의 각 스텝 뒤가 첫 안전 지점이다(D-45).
            m_canvas->FlushPendingDestroy();
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
