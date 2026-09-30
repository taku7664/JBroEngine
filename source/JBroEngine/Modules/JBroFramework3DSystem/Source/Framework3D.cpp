#include <JBro/Framework3DSystem/Framework3D.h>

#include "Rendering/RenderBridge3D.h"

#include <JBro/Framework3DSystem/BuiltinComponentTypes3D.h>
#include <JBro/Framework3D/Internal/ScriptModuleContext.h>
#include <JBro/Framework3DSystem/System/Audio3DSystem.h>
#include <JBro/Framework3DSystem/System/Text3DSystem.h>
#include <JBro/Framework3D/BuiltinComponentProperties3D.h>
#include <JBro/Asset/Asset.h>
#include <JBro/Canvas/CanvasReflection.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Host/TimeSystem.h>

#include <cmath>
#include <new>
#include <utility>

namespace JBro
{
    Framework3D::~Framework3D()
    {
        Shutdown();
    }

    bool Framework3D::Initialize(const FrameworkContext& context)
    {
        if (m_initialized
            || context.time == nullptr
            || (context.renderer != nullptr && false == context.renderer->IsInitialized()))
        {
            return false;
        }

        // 빌트인 컴포넌트가 자기 프로퍼티를 이름으로 내놓을 수 있게 한다.
        // 이름표(NameTable)를 쓰므로 캔버스보다 먼저, 프레임이 돌기 전에 해 둔다.
        Component::RegisterBuiltinComponentProperties3D();
        // 이름으로 붙이는 길도 함께 연다. 씬 파일을 읽는 쪽이 이것을 쓴다.
        Component::RegisterBuiltinComponentTypes3D();
        JAllocator allocator = context.memory.persistent;
        if (allocator.allocate == nullptr || allocator.free == nullptr)
        {
            allocator = CreateDefaultAllocator();
        }

        m_context = context;
        try
        {
            // 렌더러가 없으면(시스템 테스트) 용량 하나는 두어 제출이 곧장 버려지지 않게 한다.
            const std::size_t capacity = context.renderer != nullptr ? 16384 : 64;
            if (false == m_renderWorld.ReserveMeshes(capacity) || false == m_renderWorld.ReserveTexts(capacity)
                || false == m_meshes.Initialize(context.renderer))
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

        m_initialized = true;
        return true;
    }

    bool Framework3D::BindScriptContexts() noexcept
    {
        if (false == m_initialized)
        {
            return false;
        }
        m_scriptSystems = {};
        // 텍스트 시스템은 늘 선다(CreateDefaultSystems). 없으면 서비스가 아무것도 하지 않을 뿐이다.
        m_scriptSystems.Text3D = m_canvas->GetSystems().FindSystem<System::Text3DSystem>();
        m_scriptServices = {};
        BindFramework3DSystemContext(m_scriptSystems);
        BindFramework3DServiceContext(m_scriptServices);
        // 같은 값을 블록으로도 내어 준다. 호스트가 그대로 DLL 에 건넨다.
        m_scriptBlocks[0] = MakeFramework3DSystemContextBlock(m_scriptSystems);
        m_scriptBlocks[1] = MakeFramework3DServiceContextBlock(m_scriptServices);
        m_scriptBlockCount = 2;
        return true;
    }

    JArrayView<ScriptContextBlock> Framework3D::GetScriptContextBlocks() const noexcept
    {
        return {m_scriptBlocks, m_scriptBlockCount};
    }

    void Framework3D::UnbindScriptContexts() noexcept
    {
        m_scriptBlockCount = 0;
        if (m_canvas.Get() == nullptr)
        {
            return;
        }
        auto* texts = m_canvas->GetSystems().FindSystem<System::Text3DSystem>();
        if (texts != nullptr && GetFramework3DSystems().Text3D == texts)
        {
            BindFramework3DSystemContext({});
            BindFramework3DServiceContext({});
        }
    }

    void Framework3D::Update()
    {
        if (false == m_initialized)
        {
            return;
        }

        m_canvas->BeginFrame();
        m_renderWorld.BeginFrame();
        // **멈춰 있으면 시간이 흐르지 않는다**(D-131, D-242). 2D 와 같다 - 전에는 3D 만 멈춘 동안에도 고정 스텝과 델타를 돌렸다.
        // 한 프레임 진행은 3D 에 켤 스크립트·물리가 아직 없어 시간만 한 스텝 간다.
        const bool simulating = m_simulationEnabled || m_context.time->IsStepFrame();
        if (simulating)
        {
            RunFixedSteps();
        }
        m_canvas->GetSystems().Update(*m_canvas, simulating ? m_context.time->GetFrameTime().deltaTime : 0.0f);
        m_canvas->FlushPendingDestroy();
        m_renderWorld.EndFrame();
    }

    RenderResult Framework3D::Render()
    {
        if (false == m_initialized || m_context.renderer == nullptr)
        {
            return RenderResult::Failed;
        }
        return Internal::SubmitRenderWorld3D(m_renderWorld, *m_context.renderer, m_context.debugDraw);
    }

    RenderResult Framework3D::RenderEditorView(const EditorViewDesc& view)
    {
        if (false == m_initialized || m_context.renderer == nullptr)
        {
            return RenderResult::Failed;
        }
        return Internal::SubmitEditorView3D(m_renderWorld, *m_context.renderer, view, m_context.debugDraw);
    }

    namespace
    {
        void BindComponentAssetsVisitor(const PropertyTable& table, void* instance, void* user)
        {
            auto* binding = static_cast<std::pair<AssetSystem*, Array<AssetHandle>*>*>(user);
            binding->first->BindComponentAssets(table, instance, *binding->second);
        }

        void CollectComponentAssetIdsVisitor(const PropertyTable& table, void* instance, void* user)
        {
            AssetSystem::CollectComponentAssetIds(table, instance, *static_cast<Array<AssetId>*>(user));
        }
    }

    void Framework3D::CollectCanvasAssetIds(Array<AssetId>& ids)
    {
        if (m_context.assets == nullptr || m_canvas.Get() == nullptr)
        {
            return;
        }
        ForEachReflectedInstance(*m_canvas, &CollectComponentAssetIdsVisitor, &ids);
    }

    void Framework3D::BindCanvasAssets()
    {
        if (m_context.assets == nullptr || m_canvas.Get() == nullptr)
        {
            return;
        }
        m_context.assets->ReleaseAll(m_canvasAssets);
        std::pair<AssetSystem*, Array<AssetHandle>*> binding(m_context.assets, &m_canvasAssets);
        ForEachReflectedInstance(*m_canvas, &BindComponentAssetsVisitor, &binding);
        m_context.assets->CollectUnused();
    }

    void Framework3D::Shutdown()
    {
        // 캔버스가 잡던 에셋을 놓는다. 에셋 시스템은 호스트가 프레임워크 뒤에 내리므로 아직 살아 있다.
        if (m_context.assets != nullptr)
        {
            m_context.assets->ReleaseAll(m_canvasAssets);
        }
        m_canvasAssets.Clear();
        m_canvas.Reset();
        m_meshes.Shutdown();
        m_renderWorld = {};
        m_context = {};
        m_initialized = false;
    }

    std::size_t Framework3D::ReleaseModuleScripts()
    {
        return m_canvas.Get() != nullptr ? m_canvas->ReleaseModuleScripts() : 0;
    }

    Canvas* Framework3D::GetCanvas()
    {
        return m_canvas.Get();
    }

    RenderWorld3D* Framework3D::GetRenderWorld()
    {
        return &m_renderWorld;
    }

    MeshLibrary& Framework3D::GetMeshLibrary()
    {
        return m_meshes;
    }

    void Framework3D::CreateDefaultSystems()
    {
        auto& systems = m_canvas->GetSystems();
        systems.AddSystem<System::Transform3DSystem>();
        systems.AddSystem<System::Camera3DSystem>().SetRenderWorld(&m_renderWorld);
        auto& meshes = systems.AddSystem<System::MeshRender3DSystem>();
        meshes.SetRenderWorld(&m_renderWorld);
        meshes.SetMeshLibrary(&m_meshes);
        // 3D 텍스트(D-222). 폰트는 에셋 시스템에서, 페이지는 렌더러로 간다 - 둘 중 하나가 없으면(시스템 테스트) 그리지 않는다.
        auto& texts = systems.AddSystem<System::Text3DSystem>();
        texts.SetRenderWorld(&m_renderWorld);
        texts.SetResources(m_context.assets, m_context.renderer, m_context.tasks);
        // 오디오가 있으면 소스·리스너 시스템을 세운다(D-197).
        if (m_context.audio != nullptr)
        {
            systems.AddSystem<System::Audio3DSystem>(*m_context.audio).SetEnabled(m_simulationEnabled);
        }
    }

    void Framework3D::SetSimulationEnabled(bool enabled)
    {
        m_simulationEnabled = enabled;
        if (m_canvas.Get() == nullptr)
        {
            return;
        }
        // 멈추면 보이스를 멈추고 소스를 처음으로 되돌린다 - 다시 켜면 `playOnStart` 가 한 번 다시 울린다.
        if (System::Audio3DSystem* audio = m_canvas->GetSystems().FindSystem<System::Audio3DSystem>())
        {
            if (false == enabled)
            {
                audio->ReleaseAllSources(*m_canvas);
            }
            audio->SetEnabled(enabled);
        }
    }

    void Framework3D::RunFixedSteps()
    {
        System::TimeSystem& time = *m_context.time;
        const FrameTime& frame = time.GetFrameTime();
        for (std::uint32_t step = 0; step < frame.fixedStepCount; ++step)
        {
            time.BeginFixedStep();
            m_canvas->GetSystems().FixedUpdate(*m_canvas, frame.fixedDeltaTime);
            // 고정 스텝 묶음의 각 스텝 뒤가 첫 안전 지점이다(D-45).
            m_canvas->FlushPendingDestroy();
        }
        time.EndFixedSteps();
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
