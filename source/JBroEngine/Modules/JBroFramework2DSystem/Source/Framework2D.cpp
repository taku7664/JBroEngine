#include <JBro/Framework2DSystem/Framework2D.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Core/Profiler.h>
#include <JBro/Canvas/CanvasReflection.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Framework2DSystem/BuiltinComponentTypes2D.h>
#include <JBro/Framework2D/BuiltinComponentProperties2D.h>
#include <JBro/Framework2D/Internal/ScriptModuleContext.h>
#include <JBro/Framework2D/Internal/SystemContext.h>
#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Framework2DSystem/Network/Transform2DReplication.h>
#include <JBro/NetworkSystem/NetworkHost.h>
#include <JBro/NetworkSystem/System/NetworkSystems.h>
#include "Rendering/RenderBridge2D.h"

#include <cmath>
#include <new>
#include <utility>

namespace JBro
{
    // 2D 가 복제하는 풀은 `Transform2D` 하나다. 더 생기면 여기에 늘고, 등록 순서가 와이어의 타입 번호다.
    struct Framework2DNetworkBinding
    {
        explicit Framework2DNetworkBinding(Canvas& canvas)
            : transforms(canvas)
        {
        }

        Transform2DReplicatedPool transforms;
    };

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
        // 빌트인 컴포넌트가 자기 프로퍼티를 이름으로 내놓을 수 있게 한다.
        // 이름표(NameTable)를 쓰므로 캔버스보다 먼저, 프레임이 돌기 전에 해 둔다.
        Component::RegisterBuiltinComponentProperties2D();
        // 이름으로 붙이는 길도 함께 연다. 씬 파일을 읽는 쪽이 이것을 쓴다.
        Component::RegisterBuiltinComponentTypes2D();
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
            // 네트워크가 있으면 캔버스를 묶고 복제 풀을 등록한다(D-122). 수신·송신 시스템은 CreateDefaultSystems 가 세운다.
            if (m_context.network != nullptr)
            {
                m_context.network->BindCanvas(m_canvas.Get());
                m_networkBinding = MakeOwnerPtr<Framework2DNetworkBinding>(*m_canvas);
                m_context.network->RegisterPool(m_networkBinding->transforms);
            }
            m_spriteLibrary.Initialize(context.assets, context.renderer);
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
        // **멈춰 있으면 시간이 흐르지 않는다**(D-131). 고정 스텝을 돌리지 않고 dt 를 0 으로
        // 넘긴다 - 스크립트·물리는 `SetSimulationEnabled` 가 이미 세워 두었고, 남은 것은
        // 트랜스폼과 추출이라 시간이 필요 없다. 그래도 **돌리기는 한다**: 편집 중에도
        // 화면은 나와야 하고, 그림은 추출한 것에서 나온다.
        if (m_simulationEnabled)
        {
            const ProfileScope scope("FixedSteps");
            RunFixedSteps(deltaTime);
        }
        {
            const ProfileScope scope("Systems");
            m_canvas->GetSystems().Update(*m_canvas, m_simulationEnabled ? deltaTime : 0.0f);
        }
        m_canvas->FlushPendingDestroy();
        m_renderWorld.EndFrame();
    }

    void Framework2D::SetSimulationEnabled(bool enabled)
    {
        m_simulationEnabled = enabled;
        ApplySimulationEnabled();
    }

    void Framework2D::ApplySimulationEnabled()
    {
        if (m_canvas.Get() == nullptr)
        {
            return;
        }
        // **게임을 움직이는 것만 세운다.** 트랜스폼·카메라·스프라이트 추출은 그대로 돈다 -
        // 그것까지 세우면 편집 화면이 빈 화면이 된다.
        SystemScheduler& systems = m_canvas->GetSystems();
        if (System::ScriptSystem* scripts = systems.FindSystem<System::ScriptSystem>())
        {
            scripts->SetEnabled(m_simulationEnabled);
        }
        if (System::Physics2DSystem* physics = systems.FindSystem<System::Physics2DSystem>())
        {
            physics->SetEnabled(m_simulationEnabled);
        }
        if (System::NetworkReceiveSystem* receive = systems.FindSystem<System::NetworkReceiveSystem>())
        {
            receive->SetEnabled(m_simulationEnabled);
        }
        if (System::NetworkSendSystem* send = systems.FindSystem<System::NetworkSendSystem>())
        {
            send->SetEnabled(m_simulationEnabled);
        }
    }

    RenderResult Framework2D::Render()
    {
        if (false == m_initialized || m_context.renderer == nullptr)
        {
            return RenderResult::Failed;
        }
        return Internal::SubmitRenderWorld2D(m_renderWorld, *m_context.renderer);
    }

    RenderResult Framework2D::RenderEditorView(const EditorViewDesc& view)
    {
        if (false == m_initialized || m_context.renderer == nullptr)
        {
            return RenderResult::Failed;
        }
        return Internal::SubmitEditorView2D(m_renderWorld, *m_context.renderer, view);
    }

    namespace
    {
        void BindComponentAssetsVisitor(const PropertyTable& table, ComponentBase& component, void* user)
        {
            auto* binding = static_cast<std::pair<AssetSystem*, Array<AssetHandle>*>*>(user);
            binding->first->BindComponentAssets(table, &component, *binding->second);
        }
    }

    void Framework2D::BindCanvasAssets()
    {
        if (m_context.assets == nullptr || m_canvas.Get() == nullptr)
        {
            return;
        }
        m_context.assets->ReleaseAll(m_canvasAssets);
        std::pair<AssetSystem*, Array<AssetHandle>*> binding(m_context.assets, &m_canvasAssets);
        ForEachReflectedComponent(*m_canvas, &BindComponentAssetsVisitor, &binding);
        m_context.assets->CollectUnused();
    }

    void Framework2D::Shutdown()
    {
        // 캔버스가 잡던 에셋을 놓는다. 에셋 시스템은 호스트가 프레임워크 뒤에 내리므로 아직 살아 있다.
        if (m_context.assets != nullptr)
        {
            m_context.assets->ReleaseAll(m_canvasAssets);
        }
        m_canvasAssets.Clear();
        UnbindScriptContexts();
        // 네트워크가 캔버스를 잊는다. 트랜스포트는 남는다 - 연결은 캔버스보다 오래 산다.
        if (m_context.network != nullptr)
        {
            m_context.network->UnbindCanvas();
        }
        m_networkBinding.Reset();
        // Canvas shuts down its systems before objects/components and render storage disappear.
        m_layer2DStates.Clear();
        m_canvas.Reset();
        m_spriteLibrary.Shutdown();
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

    SpriteLibrary* Framework2D::GetSpriteLibrary()
    {
        return &m_spriteLibrary;
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
        System::SpriteRender2DSystem& sprites = systems.AddSystem<System::SpriteRender2DSystem>();
        sprites.SetRenderWorld(&m_renderWorld);
        sprites.SetSpriteLibrary(&m_spriteLibrary);
        // 수신은 가장 앞(50), 송신은 가장 뒤(500)다. 한 시스템이면 물리보다 앞이면서 뒤일 수 없다(network-plan §2.6).
        if (m_context.network != nullptr)
        {
            systems.AddSystem<System::NetworkReceiveSystem>(*m_context.network);
            systems.AddSystem<System::NetworkSendSystem>(*m_context.network);
        }
        // 시스템이 막 섰다. 지금 정해져 있는 값을 그대로 적용한다 - 프로젝트를 열기 전에
        // 꺼 두었으면 첫 프레임부터 꺼져 있어야 한다.
        ApplySimulationEnabled();
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
