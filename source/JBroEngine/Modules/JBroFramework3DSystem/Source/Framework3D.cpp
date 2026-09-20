#include <JBro/Framework3DSystem/Framework3D.h>

#include "Rendering/RenderBridge3D.h"

#include <JBro/Framework3DSystem/BuiltinComponentTypes3D.h>
#include <JBro/Framework3D/BuiltinComponentProperties3D.h>
#include <JBro/Asset/Asset.h>
#include <JBro/Canvas/CanvasReflection.h>
#include <JBro/Graphics/Renderer.h>

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
            if (false == m_renderWorld.ReserveMeshes(capacity) || false == m_meshes.Initialize(context.renderer))
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
        m_renderWorld.BeginFrame();
        RunFixedSteps(deltaTime);
        m_canvas->GetSystems().Update(*m_canvas, deltaTime);
        m_canvas->FlushPendingDestroy();
        m_renderWorld.EndFrame();
    }

    RenderResult Framework3D::Render()
    {
        if (false == m_initialized || m_context.renderer == nullptr)
        {
            return RenderResult::Failed;
        }
        return Internal::SubmitRenderWorld3D(m_renderWorld, *m_context.renderer);
    }

    RenderResult Framework3D::RenderEditorView(const EditorViewDesc& view)
    {
        if (false == m_initialized || m_context.renderer == nullptr)
        {
            return RenderResult::Failed;
        }
        return Internal::SubmitEditorView3D(m_renderWorld, *m_context.renderer, view);
    }

    namespace
    {
        void BindComponentAssetsVisitor(const PropertyTable& table, ComponentBase& component, void* user)
        {
            auto* binding = static_cast<std::pair<AssetSystem*, Array<AssetHandle>*>*>(user);
            binding->first->BindComponentAssets(table, &component, *binding->second);
        }
    }

    void Framework3D::BindCanvasAssets()
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
        m_fixedAccumulator = 0.0;
        m_initialized = false;
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
