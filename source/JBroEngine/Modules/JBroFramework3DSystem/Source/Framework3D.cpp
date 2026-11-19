#include <JBro/Framework3DSystem/Framework3D.h>
#include <JBro/Runtime/SystemContext.h>

#include "Rendering/RenderBridge3D.h"

#include <JBro/Framework3DSystem/BuiltinComponentTypes3D.h>
#include <JBro/Framework3D/Internal/ScriptModuleContext.h>
#include <JBro/Framework3DSystem/System/Audio3DSystem.h>
#include <JBro/Framework3DSystem/System/Physics3DSystem.h>
#include <JBro/Framework3DSystem/System/Light3DSystem.h>
#include <JBro/Framework3DSystem/System/Text3DSystem.h>
#include <JBro/Framework3D/BuiltinComponentProperties3D.h>
#include <JBro/Asset/Asset.h>
#include <JBro/Canvas/CanvasReflection.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Host/TimeSystem.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework3D/Component/PostProcessVolume3D.h>
#include <JBro/Framework3DSystem/Rendering/PostProcessVolume3DWeight.h>
#include <JBro/Framework2D/BuiltinComponentProperties2D.h>
#include <JBro/Input/InputSystem.h>
#include <JBro/ScreenUi/Rendering/SpriteSubmit2D.h>
#include <JBro/ScreenUi/ScreenUiComponentTypes.h>
#include <JBro/ScriptRuntime/ScriptSystem.h>
#include <JBro/ScreenUi/System/Button2DSystem.h>
#include <JBro/ScreenUi/System/Layout2DSystem.h>
#include <JBro/ScreenUi/System/Mask2DSystem.h>
#include <JBro/ScreenUi/System/SpriteRender2DSystem.h>
#include <JBro/ScreenUi/System/Text2DSystem.h>
#include <JBro/ScreenUi/System/Transform2DSystem.h>
#include <JBro/ScreenUi/System/Tween2DSystem.h>

#include <cmath>
#include <new>
#include <utility>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/Float.h>

namespace JBro
{
    Framework3D::~Framework3D()
    {
        Shutdown();
    }

    Bool Framework3D::Initialize(const FrameworkContext& context)
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
        // 화면 레이어의 2D 컴포넌트(D-362). 프로퍼티는 2D 의 것 전부이고(이름표일 뿐이다), 붙이는 길은 화면 UI 묶음만이다.
        Component::RegisterBuiltinComponentProperties2D();
        Component::RegisterScreenUiComponentTypes();
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
                || false == m_renderWorld.ReserveLights(256)
                || false == m_meshes.Initialize(context.renderer, context.assets)
                || false == m_meshMaterials.Initialize(context.renderer, context.assets))
            {
                Shutdown();
                return false;
            }
            // 화면 레이어의 스프라이트는 렌더러가 한 프레임에 받는 만큼이다 - 2D 와 같다.
            const auto sprites = context.renderer != nullptr ? context.renderer->GetSpriteSubmissionLimit() : UInt32(0);
            if (false == m_screenWorld.ReserveSprites(sprites))
            {
                Shutdown();
                return false;
            }
            m_sprites.Initialize(context.assets, context.renderer);
            m_materials.Initialize(context.assets, context.renderer, context.shaderCompiler);
            m_postProcess.Initialize(context.assets, context.renderer, &m_materials);
            m_postMixer.Reserve(PostMixerEntries, PostMixerBlockBytes);
            m_volumes.Reserve(MaxVolumes);
            m_renderWorld.ReserveLayerPostProcess(LayerPostProcessCapacity);
            m_postProcessSystem.Bind(&m_postMixer);
            m_materialSystem.Bind(&m_materials);
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

    Bool Framework3D::BindScriptContexts() noexcept
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
        // 오브젝트 서비스(D-301). 공통 시스템 컨텍스트의 이 모듈 사본에 꽂는다 - DLL 은 싣는 순간 이 컨텍스트를 받아 간다.
        m_objects.Bind(m_canvas.Get(), "Component::Transform3D");
        {
            SystemContext systems = GetSystemContext();
            systems.Objects = &m_objects;
            systems.PostProcess = &m_postProcessSystem;
            systems.Material = &m_materialSystem;
            BindSystemContext(systems);
        }
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
        if (GetSystemContext().Objects == &m_objects)
        {
            SystemContext systems = GetSystemContext();
            systems.Objects = nullptr;
            BindSystemContext(systems);
        }
        if (GetSystemContext().PostProcess == &m_postProcessSystem)
        {
            SystemContext systems = GetSystemContext();
            systems.PostProcess = nullptr;
            BindSystemContext(systems);
        }
        if (GetSystemContext().Material == &m_materialSystem)
        {
            SystemContext systems = GetSystemContext();
            systems.Material = nullptr;
            BindSystemContext(systems);
        }
        m_objects.Bind(nullptr, nullptr);
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
        m_screenWorld.BeginFrame();
        // 바뀐 머티리얼을 다시 짓는다(D-347). 렌더러 프레임 밖이다.
        m_materials.Update();
        System::Button2DSystem* buttons = m_canvas->GetSystems().FindSystem<System::Button2DSystem>();
        {
            // 화면 기준을 갱신 전에 건다 - 트랜스폼이 이번 프레임의 앵커를 잰다(D-237). 3D 카메라에는 레터박스가 없어 화면 전체다.
            ScreenSpaceFrame frame = m_screenSpace;
            if ((false == (frame.targetWidth > 0.0f) || false == (frame.targetHeight > 0.0f)) && m_context.renderer != nullptr)
            {
                const Extent2D extent = m_context.renderer->GetFrameExtent();
                frame.targetWidth = static_cast<JBro::Float>(extent.width);
                frame.targetHeight = static_cast<JBro::Float>(extent.height);
            }
            m_screenWorld.SetScreenSpace(frame);
            if (System::Transform2DSystem* transforms = m_canvas->GetSystems().FindSystem<System::Transform2DSystem>())
            {
                transforms->SetScreenSpace(frame);
            }
            if (buttons != nullptr)
            {
                buttons->SetScreenSpace(frame);
            }
        }
        // **멈춰 있으면 시간이 흐르지 않는다**(D-131, D-242). 2D 와 같다 - 전에는 3D 만 멈춘 동안에도 고정 스텝과 델타를 돌렸다.
        // **한 프레임 진행**(D-242). 멈춘 동안 이 프레임만 스크립트를 켰다가 끝에 다시 끈다(2D 와 같다). 소리는 멈춘 그대로다.
        const Bool stepping = false == m_simulationEnabled && m_context.time->IsStepFrame();
        const Bool simulating = m_simulationEnabled || stepping;
        if (stepping)
        {
            ApplySimulationEnabled(true);
        }
        // **입력 체인은 고정 스텝보다 먼저다**(D-214·D-363). 스크립트의 핸들러와 위젯(`"UI"` 레이어의 버튼 시스템)이 한 체인에 선다 - 위젯이 쓴 장치는
        // 소비되어 아래 레이어와 `InputService` 의 폴링이 빈 포인터를 본다. 멈춰 있으면 돌지 않는다.
        if (simulating)
        {
            if (System::ScriptSystem* scripts = m_canvas->GetSystems().FindSystem<System::ScriptSystem>())
            {
                scripts->DispatchInput(*m_canvas);
            }
        }
        if (simulating)
        {
            RunFixedSteps();
        }
        m_canvas->GetSystems().Update(*m_canvas, simulating ? m_context.time->GetFrameTime().deltaTime : Float(0.0f));
        m_canvas->FlushPendingDestroy();
        if (stepping)
        {
            ApplySimulationEnabled(false);
        }
        m_renderWorld.EndFrame();
        m_screenWorld.EndFrame();
        ResolvePostProcess();
    }

    void Framework3D::ResolvePostProcess()
    {
        // 렌더러 프레임 밖이다 - LUT 를 올릴 수 있다(D-344).
        const RenderCamera3D* camera = m_renderWorld.GetCamera();
        if (camera == nullptr)
        {
            m_renderWorld.SetPostProcess({});
        }
        else
        {
            // 카메라의 프로필이 기준이고, 카메라 자리의 볼륨을 우선순위가 낮은 것부터 섞은 뒤 스크립트의 덮어쓰기를 얹는다(D-346).
            m_postMixer.Begin(m_postProcess.Resolve(camera->postProcessProfile));
            m_volumes.Clear();
            Canvas& canvas = *m_canvas;
            canvas.ForEach<Component::PostProcessVolume3D>([&](Component::PostProcessVolume3D& volume) {
                if (false == volume.IsActiveComponent() || volume.postProcessProfile.generation == 0)
                {
                    return;
                }
                Object::GameObject* owner = Internal::CanvasAccess::GetOwner(volume);
                const auto* transform = canvas.FindComponentRaw<Component::Transform3D>(owner);
                const Bool placed = transform != nullptr && transform->IsActiveComponent() && transform->worldValid;
                const Float weight = ComputePostProcessVolumeWeight3D(volume, placed ? transform : nullptr, camera->position);
                if (weight <= 0.0f || m_volumes.Size() >= m_volumes.Capacity())
                {
                    return;
                }
                m_volumes.Add({volume.priority, weight, volume.postProcessProfile});
            });
            for (std::size_t at = 1; at < m_volumes.Size(); ++at)
            {
                const VolumeSample moving = m_volumes[at];
                std::size_t to = at;
                while (to > 0 && m_volumes[to - 1].priority > moving.priority)
                {
                    m_volumes[to] = m_volumes[to - 1];
                    --to;
                }
                m_volumes[to] = moving;
            }
            for (const VolumeSample& sample : m_volumes)
            {
                m_postMixer.Blend(m_postProcess.ResolveProfile(sample.profile), sample.weight);
            }
            m_postMixer.ApplyOverrides();
            m_renderWorld.SetPostProcess(m_postMixer.GetResult());
        }
        // 레이어의 프로필(D-345)은 차례 자리에 건다. 3D 레이어는 레이어마다 뷰라 그 뷰를 한 장으로 얹으며 건다.
        const std::size_t layerCount = m_canvas->GetLayerCount();
        m_renderWorld.SetLayerPostProcessCount(layerCount);
        for (std::size_t index = 0; index < layerCount; ++index)
        {
            const Layer* layer = m_canvas->GetLayerAt(index);
            if (layer == nullptr || layer->GetPostProcessProfile().IsNull())
            {
                continue;
            }
            m_renderWorld.SetLayerPostProcess(layer->GetOrder(), m_postProcess.Resolve(m_postProcess.FindOrLoad(layer->GetPostProcessProfile())));
        }
    }

    RenderResult Framework3D::Render()
    {
        if (false == m_initialized || m_context.renderer == nullptr)
        {
            return RenderResult::Failed;
        }
        const RenderResult world = Internal::SubmitRenderWorld3D(m_renderWorld, *m_context.renderer, m_context.debugDraw);
        if (world == RenderResult::Failed)
        {
            return world;
        }
        // **화면 레이어는 3D 뷰를 다 그린 뒤다**(D-362). 후처리도 끝난 위라 노출·톤맵을 받지 않고, 빛도 받지 않는다. 카메라가 없어도 그린다.
        Bool screenSubmitted = false;
        if (false == Internal::SubmitScreenViews(m_screenWorld, *m_context.renderer, m_screenWorld.GetScreenSpace(), screenSubmitted))
        {
            return RenderResult::Failed;
        }
        return world == RenderResult::Submitted || screenSubmitted ? RenderResult::Submitted : RenderResult::NothingToSubmit;
    }

    void Framework3D::SetScreenSpace(const ScreenSpaceFrame& frame)
    {
        m_screenSpace = frame;
    }

    RenderResult Framework3D::RenderEditorView(const EditorViewDesc& view)
    {
        if (false == m_initialized || m_context.renderer == nullptr)
        {
            return RenderResult::Failed;
        }
        return Internal::SubmitEditorView3D(m_renderWorld, *m_context.renderer, view, m_context.debugDraw);
    }

    RenderResult Framework3D::RenderLayerThumbnail(const LayerThumbnailDesc& thumbnail)
    {
        const Layer* layer = m_canvas.Get() != nullptr ? m_canvas->FindLayer(thumbnail.layer) : nullptr;
        if (false == m_initialized || m_context.renderer == nullptr || layer == nullptr)
        {
            return RenderResult::NothingToSubmit;
        }
        return Internal::SubmitLayerThumbnail3D(m_renderWorld, *m_context.renderer, thumbnail, layer->GetOrder());
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
        m_meshMaterials.Shutdown();
        m_meshes.Shutdown();
        m_sprites.Shutdown();
        m_screenWorld = {};
        m_postProcess.Shutdown();
        m_materials.Shutdown();
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

    MeshMaterialLibrary& Framework3D::GetMeshMaterialLibrary()
    {
        return m_meshMaterials;
    }

    MeshLibrary& Framework3D::GetMeshLibrary()
    {
        return m_meshes;
    }

    RenderWorld2D* Framework3D::GetScreenRenderWorld()
    {
        return &m_screenWorld;
    }

    void Framework3D::CreateDefaultSystems()
    {
        auto& systems = m_canvas->GetSystems();
        systems.AddSystem<System::Transform3DSystem>();
        systems.AddSystem<System::Camera3DSystem>().SetRenderWorld(&m_renderWorld);
        auto& meshes = systems.AddSystem<System::MeshRender3DSystem>();
        meshes.SetRenderWorld(&m_renderWorld);
        meshes.SetMeshLibrary(&m_meshes);
        meshes.SetMaterialLibrary(&m_materials);
        meshes.SetMeshMaterialLibrary(&m_meshMaterials);
        // 3D 텍스트(D-222). 폰트는 에셋 시스템에서, 페이지는 렌더러로 간다 - 둘 중 하나가 없으면(시스템 테스트) 그리지 않는다.
        systems.AddSystem<System::Light3DSystem>().SetRenderWorld(&m_renderWorld);
        // **3D 물리**(D-416). 고정 스텝에서 돈다 - 재생 중에만 시간이 흐른다.
        systems.AddSystem<System::Physics3DSystem>();
        auto& texts = systems.AddSystem<System::Text3DSystem>();
        texts.SetRenderWorld(&m_renderWorld);
        texts.SetResources(m_context.assets, m_context.renderer, m_context.tasks);
        // **스크립트**(D-363). 2D 와 같은 시스템이다 - 실행 순서·수명 훅·입력 레이어 체인을 돈다.
        System::ScriptSystem& scripts = systems.AddSystem<System::ScriptSystem>();
        scripts.SetInputSystem(m_context.input);
        // **화면 레이어의 화면 UI**(D-362). 2D 와 같은 시스템이다. 위젯은 스크립트의 입력 체인 `"UI"` 레이어에 서고, 훅은 `GameScript3D` 로 부른다(D-363).
        // 월드 레이어의 역투영은 넘기지 않는다 - 3D 캔버스의 2D 컴포넌트는 화면 레이어에만 둔다.
        systems.AddSystem<System::Transform2DSystem>();
        systems.AddSystem<System::Mask2DSystem>();
        {
            auto& layout = systems.AddSystem<System::Layout2DSystem>();
            layout.SetSpriteLibrary(&m_sprites);
            layout.SetHookDispatcher(&m_uiHooks);
        }
        systems.AddSystem<System::Tween2DSystem>();
        {
            auto& buttons = systems.AddSystem<System::Button2DSystem>();
            buttons.SetTextInputHost(m_context.textInput);
            buttons.SetInputChain(&scripts);
            buttons.SetHookDispatcher(&m_uiHooks);
        }
        System::SpriteRender2DSystem& screenSprites = systems.AddSystem<System::SpriteRender2DSystem>();
        screenSprites.SetRenderWorld(&m_screenWorld);
        screenSprites.SetSpriteLibrary(&m_sprites);
        screenSprites.SetMaterialLibrary(&m_materials);
        System::Text2DSystem& screenTexts = systems.AddSystem<System::Text2DSystem>();
        screenTexts.SetRenderWorld(&m_screenWorld);
        screenTexts.SetResources(m_context.assets, m_context.renderer, m_context.tasks);
        // 오디오가 있으면 소스·리스너 시스템을 세운다(D-197).
        if (m_context.audio != nullptr)
        {
            systems.AddSystem<System::Audio3DSystem>(*m_context.audio).SetEnabled(m_simulationEnabled);
        }
        // 시스템이 막 섰다. 지금 정해져 있는 값을 그대로 적용한다 - 프로젝트를 열기 전에 꺼 두었으면 첫 프레임부터 꺼져 있어야 한다.
        if (System::ScriptSystem* created = systems.FindSystem<System::ScriptSystem>())
        {
            created->SetEnabled(m_simulationEnabled);
        }
    }

    void Framework3D::ApplySimulationEnabled(Bool enabled)
    {
        if (m_canvas.Get() == nullptr)
        {
            return;
        }
        if (System::ScriptSystem* scripts = m_canvas->GetSystems().FindSystem<System::ScriptSystem>())
        {
            scripts->SetEnabled(enabled);
        }
    }

    void Framework3D::PrepareBreakFrames()
    {
        if (false == m_initialized || m_canvas.Get() == nullptr)
        {
            return;
        }
        // 스케줄러는 다시 들어가지 못한다 - 지금 그 안에서 스크립트가 서 있다(D-330). 시뮬레이션과 무관한 추출 시스템만 차례대로 직접 부른다.
        // 스크립트를 부르는 시스템(스크립트·버튼)은 모두 추출보다 앞이라, 선 자리는 늘 이번 프레임의 추출 전이다.
        SystemScheduler& systems = m_canvas->GetSystems();
        GameSystem* const extraction[] = {
            systems.FindSystem<System::Transform3DSystem>(),
            systems.FindSystem<System::Transform2DSystem>(),
            systems.FindSystem<System::Camera3DSystem>(),
            systems.FindSystem<System::MeshRender3DSystem>(),
            systems.FindSystem<System::Text3DSystem>(),
            systems.FindSystem<System::SpriteRender2DSystem>(),
            systems.FindSystem<System::Text2DSystem>(),
        };
        m_renderWorld.BeginFrame();
        m_screenWorld.BeginFrame();
        m_materials.Update();
        for (GameSystem* system : extraction)
        {
            if (system != nullptr)
            {
                system->Update(*m_canvas, 0.0f);
            }
        }
        m_renderWorld.EndFrame();
        m_screenWorld.EndFrame();
        ResolvePostProcess();
    }

    void Framework3D::EndBreakFrames()
    {
        if (m_initialized)
        {
            m_renderWorld.BeginFrame();
            m_screenWorld.BeginFrame();
        }
    }

    void Framework3D::SetSimulationEnabled(Bool enabled)
    {
        m_simulationEnabled = enabled;
        if (m_canvas.Get() == nullptr)
        {
            return;
        }
        ApplySimulationEnabled(enabled);
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
        for (UInt32 step = 0; step < frame.fixedStepCount; ++step)
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
