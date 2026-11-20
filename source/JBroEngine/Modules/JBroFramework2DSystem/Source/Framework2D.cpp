#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Runtime/SystemContext.h>
#include <JBro/ScreenUi/System/Transform2DSystem.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Core/Profiler.h>
#include <JBro/Canvas/CanvasReflection.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Host/TimeSystem.h>
#include <JBro/Framework2DSystem/BuiltinComponentTypes2D.h>
#include <JBro/Framework2D/BuiltinComponentProperties2D.h>
#include <JBro/Framework2D/Internal/ScriptModuleContext.h>
#include <JBro/Framework2D/Internal/SystemContext.h>
#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Framework2DSystem/Network/Transform2DReplication.h>
#include <JBro/Framework2DSystem/System/Audio2DSystem.h>
#include <JBro/Framework2DSystem/System/Light2DSystem.h>
#include <JBro/Framework2DSystem/System/ShadowCaster2DSystem.h>
#include <JBro/ScreenUi/System/Text2DSystem.h>
#include <JBro/ScreenUi/System/Button2DSystem.h>
#include <JBro/ScreenUi/System/Layout2DSystem.h>
#include <JBro/ScreenUi/System/Tween2DSystem.h>
#include <JBro/ScreenUi/System/Mask2DSystem.h>
#include <JBro/Framework2DSystem/System/Camera2DSystem.h>
#include <JBro/ScreenUi/System/SpriteRender2DSystem.h>
#include <JBro/Framework2DSystem/Rendering/CameraView2D.h>
#include <JBro/Framework2DSystem/Rendering/PostProcessVolume2DWeight.h>
#include <JBro/Framework2D/Component/PostProcessVolume2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/NetworkSystem/NetworkHost.h>
#include <JBro/NetworkSystem/System/NetworkSystems.h>
#include "Rendering/RenderBridge2D.h"

#include <cmath>
#include <new>
#include <utility>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/Float.h>

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

    Bool Framework2D::Initialize(const FrameworkContext& context)
    {
        if (m_initialized || context.time == nullptr
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
            const auto capacity = context.renderer != nullptr ? context.renderer->GetSpriteSubmissionLimit() : UInt32(0);
            const auto lightCapacity = context.renderer != nullptr ? context.renderer->GetLight2DLimit() : UInt32(0);
            const auto edgeCapacity = context.renderer != nullptr ? context.renderer->GetShadowEdge2DLimit() : UInt32(0);
            if (false == m_renderWorld.ReserveSprites(capacity) || false == m_renderWorld.ReserveLights(lightCapacity)
                || false == m_renderWorld.ReserveShadowEdges(edgeCapacity))
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
            m_materials.Initialize(context.assets, context.renderer, context.shaderCompiler);
            m_postProcess.Initialize(context.assets, context.renderer, &m_materials);
            m_postMixer.Reserve(PostMixerEntries, PostMixerBlockBytes);
            m_cameraMixers.Reserve(MaxCameraMixers);
            for (UInt32 index = 0; index < MaxCameraMixers; ++index)
            {
                PostProcessMixer& mixer = m_cameraMixers.Add(PostProcessMixer{});
                mixer.Reserve(PostMixerEntries, PostMixerBlockBytes);
            }
            m_volumes.Reserve(MaxVolumes);
            m_renderWorld.ReserveLayerPostProcess(LayerPostProcessCapacity);
            m_postProcessSystem.Bind(&m_postMixer);
            m_materialSystem.Bind(&m_materials);
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

    Bool Framework2D::BindScriptContexts() noexcept
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
        // 텍스트 시스템은 늘 선다(CreateDefaultSystems). 없으면 서비스가 아무것도 하지 않을 뿐이다.
        m_scriptSystems.Text2D = m_canvas->GetSystems().FindSystem<System::Text2DSystem>();
        m_scriptSystems.Screen2D = m_canvas->GetSystems().FindSystem<System::Button2DSystem>();
        m_scriptServices = {};
        BindFramework2DSystemContext(m_scriptSystems);
        BindFramework2DServiceContext(m_scriptServices);
        // 같은 값을 블록으로도 내어 준다. 호스트가 그대로 DLL 에 건넨다.
        m_scriptBlocks[0] = MakeFramework2DSystemContextBlock(m_scriptSystems);
        m_scriptBlocks[1] = MakeFramework2DServiceContextBlock(m_scriptServices);
        // 오브젝트 서비스(D-301). 공통 시스템 컨텍스트의 이 모듈 사본에 꽂는다 - DLL 은 싣는 순간 이 컨텍스트를 받아 간다.
        m_objects.Bind(m_canvas.Get(), "Component::Transform2D");
        m_viewports.Bind(m_canvas.Get());
        // 스크립트가 월드 레이어에 붙이는 위젯의 픽셀 기본값은 유닛으로 나눈다(D-355, 에디터의 D-354 와 같다).
        m_objects.SetWorldPixelsPerUnit(DefaultPixelsPerUnit);
        {
            SystemContext systems = GetSystemContext();
            systems.Objects = &m_objects;
            systems.Viewports = &m_viewports;
            systems.PostProcess = &m_postProcessSystem;
            systems.Material = &m_materialSystem;
            BindSystemContext(systems);
        }
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
        if (GetSystemContext().Viewports == &m_viewports)
        {
            SystemContext systems = GetSystemContext();
            systems.Viewports = nullptr;
            BindSystemContext(systems);
        }
        if (GetSystemContext().Material == &m_materialSystem)
        {
            SystemContext systems = GetSystemContext();
            systems.Material = nullptr;
            BindSystemContext(systems);
        }
        m_objects.Bind(nullptr, nullptr);
        m_viewports.Bind(nullptr);
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

    void Framework2D::Update()
    {
        // 무효한 델타는 호스트의 `TimeSystem::BeginFrame` 이 이미 거절했다 - 여기 오는 델타는 늘 유한하고 0 이상이다.
        if (false == m_initialized)
        {
            return;
        }
        const FrameTime& time = m_context.time->GetFrameTime();
        m_renderWorld.BeginFrame();
        // 바뀐 머티리얼(본문·셰이더·텍스처)을 추출 전에 다시 짓는다(D-347). 렌더러 프레임 밖이다.
        m_materials.Update();
        m_canvas->BeginFrame();
        {
            // 화면 기준을 갱신 전에 건다 - 트랜스폼이 이번 프레임의 앵커를 잰다(D-237).
            ScreenSpaceFrame frame = m_screenSpace;
            if ((false == (frame.targetWidth > 0.0f) || false == (frame.targetHeight > 0.0f)) && m_context.renderer != nullptr)
            {
                const Extent2D extent = m_context.renderer->GetFrameExtent();
                frame.targetWidth = static_cast<JBro::Float>(extent.width);
                frame.targetHeight = static_cast<JBro::Float>(extent.height);
            }
            // `PixelPerfect` 카메라의 레터박스를 화면 기준에 건다(D-239) - 앵커·버튼이 그 사각형 안을 잰다. 카메라는 지난 프레임의
            // 트랜스폼으로 고른다(이번 프레임의 트랜스폼은 아직 돌지 않았다). 그리기는 이번에 뽑힌 카메라로 같은 함수를 다시 부른다.
            RenderCamera2D camera;
            ApplyCameraArea(System::Camera2DSystem::SelectCamera(*m_canvas, camera) ? &camera : nullptr, frame);
            m_renderWorld.SetScreenSpace(frame);
            if (System::Transform2DSystem* transforms = m_canvas->GetSystems().FindSystem<System::Transform2DSystem>())
            {
                transforms->SetScreenSpace(frame);
            }
            // 버튼은 입력 체인 안에서 지난 프레임의 화면을 누른다 - 같은 기준이어야 그린 자리를 누른다.
            if (System::Button2DSystem* buttons = m_canvas->GetSystems().FindSystem<System::Button2DSystem>())
            {
                buttons->SetScreenSpace(frame);
            }
        }
        // **한 프레임 진행**(D-242). 멈춘 동안 이 프레임만 게임이 돈다: 스크립트·물리를 켰다가 끝에 다시 끈다.
        const Bool stepping = false == m_simulationEnabled && m_context.time->IsStepFrame();
        const Bool simulating = m_simulationEnabled || stepping;
        if (stepping)
        {
            SetSteppedSystemsEnabled(true);
        }
        // **입력 체인은 고정 스텝보다 먼저다**(D-214). `OnFixedUpdate` 가 폴링하는 입력에도 위 레이어의 블로킹이
        // 걸려야 한다 - 시스템 갱신 안(`ScriptSystem::OnUpdate`)에 두면 그보다 앞서 도는 고정 스텝이 막히기 전의 입력을 본다.
        // 멈춰 있으면 스크립트가 돌지 않으니 체인도 돌지 않는다. 그때 폴링은 이번 프레임 전체를 보지만 읽는 스크립트가 없다.
        if (simulating)
        {
            if (System::ScriptSystem* scripts = m_canvas->GetSystems().FindSystem<System::ScriptSystem>())
            {
                const ProfileScope scope("Input");
                scripts->DispatchInput(*m_canvas);
            }
        }
        // **멈춰 있으면 시간이 흐르지 않는다**(D-131). 고정 스텝을 돌리지 않고 dt 를 0 으로
        // 넘긴다 - 스크립트·물리는 `SetSimulationEnabled` 가 이미 세워 두었고, 남은 것은
        // 트랜스폼과 추출이라 시간이 필요 없다. 그래도 **돌리기는 한다**: 편집 중에도
        // 화면은 나와야 하고, 그림은 추출한 것에서 나온다.
        if (simulating)
        {
            const ProfileScope scope("FixedSteps");
            RunFixedSteps();
        }
        {
            const ProfileScope scope("Systems");
            m_canvas->GetSystems().Update(*m_canvas, simulating ? time.deltaTime : Float(0.0f));
        }
        m_canvas->FlushPendingDestroy();
        if (stepping)
        {
            SetSteppedSystemsEnabled(false);
        }
        m_renderWorld.EndFrame();
        ResolveCameraTargets();
        ResolvePostProcess();
    }

    void Framework2D::MixPostProcessFor(const RenderCamera2D& camera, PostProcessMixer& mixer, const PostProcessMixer* overridesFrom)
    {
        // 카메라의 프로필이 기준이고, 카메라 자리의 볼륨을 우선순위가 낮은 것부터 섞은 뒤 스크립트의 덮어쓰기를 얹는다(D-346).
        mixer.Begin(m_postProcess.Resolve(camera.postProcessProfile));
        m_volumes.Clear();
        Canvas& canvas = *m_canvas;
        canvas.ForEach<Component::PostProcessVolume2D>([&](Component::PostProcessVolume2D& volume) {
            if (false == volume.IsActiveComponent() || volume.postProcessProfile.generation == 0)
            {
                return;
            }
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(volume);
            const auto* transform = canvas.FindComponentRaw<Component::Transform2D>(owner);
            const Bool placed = transform != nullptr && transform->IsActiveComponent() && transform->worldValid;
            const Float weight = ComputePostProcessVolumeWeight2D(volume, placed ? transform : nullptr, camera.position);
            if (weight <= 0.0f || m_volumes.Size() >= m_volumes.Capacity())
            {
                return;
            }
            m_volumes.Add({volume.priority, weight, volume.postProcessProfile});
        });
        // 우선순위가 같으면 캔버스의 차례 그대로다(안정 정렬). 볼륨은 몇 개뿐이라 끼워 넣기로 한다.
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
            mixer.Blend(m_postProcess.ResolveProfile(sample.profile), sample.weight);
        }
        if (overridesFrom != nullptr)
        {
            mixer.CopyOverridesFrom(*overridesFrom);
        }
        mixer.ApplyOverrides();
    }

    void Framework2D::ResolveCameraTargets()
    {
        const ArrayView<const RenderCamera2D> cameras = m_renderWorld.GetCameras();
        for (std::size_t index = 0; index < cameras.Size(); ++index)
        {
            const RenderCamera2D& camera = cameras[static_cast<UInt32>(index)];
            if (camera.renderTexture.generation == 0)
            {
                continue;
            }
            AssetHandle spriteTexture;
            TextureHandle target;
            Extent2D extent;
            TextureFilter filter = TextureFilter::Linear;
            if (m_spriteLibrary.ResolveRenderTexture(camera.renderTexture, spriteTexture, target, extent, filter))
            {
                m_renderWorld.SetCameraTarget(index, target, extent, spriteTexture);
            }
            else
            {
                m_renderWorld.SetCameraTarget(index, {}, {}, {});
            }
        }
    }

    void Framework2D::ResolvePostProcess()
    {
        // 렌더러 프레임 밖이다 - LUT 를 올릴 수 있다. 카메라가 없으면 후처리도 없다.
        const RenderCamera2D* camera = m_renderWorld.GetCamera();
        if (camera == nullptr)
        {
            m_renderWorld.SetPostProcess({});
            m_renderWorld.SetCameraPostProcessCount(0);
        }
        else
        {
            MixPostProcessFor(*camera, m_postMixer, nullptr);
            m_renderWorld.SetPostProcess(m_postMixer.GetResult());
            // 분할 화면(D-361): 그릴 카메라마다 제 프로필과 제 자리의 볼륨으로 섞는다. 주 카메라는 위의 것이고, 자리를 넘는 카메라는 주 카메라의 것을 받는다.
            const ArrayView<const RenderCamera2D> cameras = m_renderWorld.GetCameras();
            const std::size_t count = cameras.Size() < m_cameraMixers.Size() ? static_cast<std::size_t>(cameras.Size()) : m_cameraMixers.Size();
            m_renderWorld.SetCameraPostProcessCount(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                const RenderCamera2D& each = cameras[static_cast<UInt32>(index)];
                if (each.owner == camera->owner)
                {
                    m_renderWorld.SetCameraPostProcess(index, m_postMixer.GetResult());
                    continue;
                }
                MixPostProcessFor(each, m_cameraMixers[index], &m_postMixer);
                m_renderWorld.SetCameraPostProcess(index, m_cameraMixers[index].GetResult());
            }
        }
        // 레이어의 프로필(D-345)은 차례 자리에 건다. 처음 보는 프로필만 싣는다.
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

    void Framework2D::SetPhysicsWorkerCount(UInt32 count)
    {
        if (m_canvas.Get() == nullptr)
        {
            return;
        }
        if (System::Physics2DSystem* physics = m_canvas->GetSystems().FindSystem<System::Physics2DSystem>())
        {
            physics->SetWorkerCount(count);
        }
    }

    void Framework2D::SetPhysicsIgnoredLayers(const UInt32 (&rows)[32])
    {
        static_assert(PhysicsLayerCount == 32, "the framework passes one row per physics layer");
        if (m_canvas.Get() == nullptr)
        {
            return;
        }
        if (System::Physics2DSystem* physics = m_canvas->GetSystems().FindSystem<System::Physics2DSystem>())
        {
            physics->SetIgnoredLayers(rows);
        }
    }

    UInt32 Framework2D::GetPhysicsWorkerCount()
    {
        if (m_canvas.Get() == nullptr)
        {
            return 0;
        }
        System::Physics2DSystem* physics = m_canvas->GetSystems().FindSystem<System::Physics2DSystem>();
        return physics != nullptr ? physics->GetWorkerCount() : UInt32(0);
    }

    void Framework2D::SetScreenSpace(const ScreenSpaceFrame& frame)
    {
        m_screenSpace = frame;
    }

    const ScreenSpaceFrame& Framework2D::GetScreenSpace() const
    {
        return m_screenSpace;
    }

    void Framework2D::SetSimulationEnabled(Bool enabled)
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
        // 소리도 게임이 움직일 때만 난다. 멈추면 보이스를 멈추고 소스를 처음으로 되돌린다 - 다시 켜면 `playOnStart` 가
        // 한 번 다시 울린다(기존 엔진의 시뮬레이션 중지 정책).
        if (System::Audio2DSystem* audio = systems.FindSystem<System::Audio2DSystem>())
        {
            if (false == m_simulationEnabled)
            {
                audio->ReleaseAllSources(*m_canvas);
            }
            audio->SetEnabled(m_simulationEnabled);
        }
    }

    void Framework2D::PrepareBreakFrames()
    {
        if (false == m_initialized || m_canvas.Get() == nullptr)
        {
            return;
        }
        // 스케줄러는 다시 들어가지 못한다 - 지금 그 안에서 스크립트가 서 있다. 시뮬레이션과 무관한 추출 시스템만 차례대로 직접 부른다.
        // 스크립트를 부르는 시스템(스크립트·버튼·물리)은 모두 추출보다 앞이라, 선 자리는 늘 이번 프레임의 추출 전이다.
        SystemScheduler& systems = m_canvas->GetSystems();
        GameSystem* const extraction[] = {
            systems.FindSystem<System::Transform2DSystem>(),
            systems.FindSystem<System::Camera2DSystem>(),
            systems.FindSystem<System::SpriteRender2DSystem>(),
            systems.FindSystem<System::Light2DSystem>(),
            systems.FindSystem<System::ShadowCaster2DSystem>(),
            systems.FindSystem<System::Text2DSystem>(),
        };
        m_renderWorld.BeginFrame();
        m_materials.Update();
        for (GameSystem* system : extraction)
        {
            if (system != nullptr)
            {
                system->Update(*m_canvas, 0.0f);
            }
        }
        m_renderWorld.EndFrame();
        ResolveCameraTargets();
        ResolvePostProcess();
    }

    void Framework2D::EndBreakFrames()
    {
        if (m_initialized)
        {
            m_renderWorld.BeginFrame();
        }
    }

    RenderResult Framework2D::Render()
    {
        if (false == m_initialized || m_context.renderer == nullptr)
        {
            return RenderResult::Failed;
        }
        return Internal::SubmitRenderWorld2D(m_renderWorld, *m_context.renderer, m_context.debugDraw);
    }

    RenderResult Framework2D::RenderEditorView(const EditorViewDesc& view)
    {
        if (false == m_initialized || m_context.renderer == nullptr)
        {
            return RenderResult::Failed;
        }
        return Internal::SubmitEditorView2D(m_renderWorld, *m_context.renderer, view, m_context.debugDraw);
    }

    RenderResult Framework2D::RenderLayerThumbnail(const LayerThumbnailDesc& thumbnail)
    {
        const Layer* layer = m_canvas.Get() != nullptr ? m_canvas->FindLayer(thumbnail.layer) : nullptr;
        if (false == m_initialized || m_context.renderer == nullptr || layer == nullptr)
        {
            return RenderResult::NothingToSubmit;
        }
        return Internal::SubmitLayerThumbnail2D(m_renderWorld, *m_context.renderer, thumbnail, *layer);
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

    void Framework2D::CollectCanvasAssetIds(Array<AssetId>& ids)
    {
        if (m_context.assets == nullptr || m_canvas.Get() == nullptr)
        {
            return;
        }
        ForEachReflectedInstance(*m_canvas, &CollectComponentAssetIdsVisitor, &ids);
        // 뷰포트의 렌더 텍스처도 게임 빌드에 든다(D-438).
        const Array<CanvasViewport>& viewports = m_canvas->GetViewports();
        for (std::size_t index = 0; index < viewports.Size(); ++index)
        {
            if (false == viewports[index].renderTextureId.IsNull())
            {
                ids.Add(viewports[index].renderTextureId);
            }
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
        ForEachReflectedInstance(*m_canvas, &BindComponentAssetsVisitor, &binding);
        // 뷰포트의 렌더 텍스처(D-438). 컴포넌트가 아니라 캔버스의 값이라 리플렉션 패스가 보지 않는다.
        m_viewportTextures.Clear();
        const Array<CanvasViewport>& viewports = m_canvas->GetViewports();
        for (std::size_t index = 0; index < viewports.Size(); ++index)
        {
            AssetHandle texture;
            if (false == viewports[index].renderTextureId.IsNull())
            {
                texture = m_context.assets->Load(viewports[index].renderTextureId);
                if (texture.generation != 0)
                {
                    m_canvasAssets.Add(texture);
                }
            }
            m_viewportTextures.Add(texture);
        }
        if (System::Camera2DSystem* cameras = m_canvas->GetSystems().FindSystem<System::Camera2DSystem>())
        {
            cameras->SetViewportTextures(ArrayView<const AssetHandle>(m_viewportTextures.Data(), m_viewportTextures.Size()));
        }
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
        m_viewportTextures.Clear();
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
        m_postProcess.Shutdown();
        m_materials.Shutdown();
        m_renderWorld = {};
        m_context     = {};
        m_initialized = false;
    }

    std::size_t Framework2D::ReleaseModuleScripts()
    {
        return m_canvas.Get() != nullptr ? m_canvas->ReleaseModuleScripts() : 0;
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

    Bool Framework2D::DestroyLayer(LayerId layer)
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

    Bool Framework2D::MoveLayer(LayerId layer, std::size_t newIndex)
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
        systems.AddSystem<System::Mask2DSystem>();
        // 레이아웃은 에셋 크기의 스프라이트를 재는 데 스프라이트 라이브러리를 쓴다(D-355).
        {
            auto& layout = systems.AddSystem<System::Layout2DSystem>();
            layout.SetSpriteLibrary(&m_spriteLibrary);
            layout.SetHookDispatcher(&m_uiHooks);
        }
        // 트윈은 위젯 뒤, 레이아웃 앞이다(D-357).
        systems.AddSystem<System::Tween2DSystem>();
        // 변환 뒤, 렌더 추출 전이다. 실행 순서는 GetExecutionOrder 가 정한다.
        System::ScriptSystem& scripts = systems.AddSystem<System::ScriptSystem>();
        scripts.SetInputSystem(m_context.input);
        // 버튼은 입력 체인의 `"UI"` 레이어에 선다(D-237).
        {
            auto& buttons = systems.AddSystem<System::Button2DSystem>();
            buttons.SetInputChain(&scripts);
            buttons.SetHookDispatcher(&m_uiHooks);
            m_worldLayers.SetCanvas(m_canvas.Get());
            buttons.SetWorldLayerMapper(&m_worldLayers);
            // 글자 칸의 IME 조합 창과 클립보드는 호스트의 것이다(D-355). 없으면(시험) 조합 창을 옮기지 않고 클립보드가 비어 있다.
            buttons.SetTextInputHost(m_context.textInput);
        }
        systems.AddSystem<System::Physics2DSystem>();
        systems.AddSystem<System::Camera2DSystem>().SetRenderWorld(&m_renderWorld);
        System::SpriteRender2DSystem& sprites = systems.AddSystem<System::SpriteRender2DSystem>();
        sprites.SetRenderWorld(&m_renderWorld);
        sprites.SetSpriteLibrary(&m_spriteLibrary);
        sprites.SetMaterialLibrary(&m_materials);
        // 라이트도 스프라이트와 같은 차례에 담는다(D-291).
        systems.AddSystem<System::Light2DSystem>().SetRenderWorld(&m_renderWorld);
        // 그림자 변은 스프라이트 크기가 풀린 뒤에 담는다(D-291 3 단계).
        System::ShadowCaster2DSystem& casters = systems.AddSystem<System::ShadowCaster2DSystem>();
        casters.SetRenderWorld(&m_renderWorld);
        casters.SetSpriteLibrary(&m_spriteLibrary);
        // 글자마다 스프라이트 아이템이다(D-200). 아틀라스와 페이지 텍스처는 이 시스템이 든다 - 캔버스가 시스템을 내릴 때
        // (렌더러보다 먼저) 풀린다.
        System::Text2DSystem& texts = systems.AddSystem<System::Text2DSystem>();
        texts.SetRenderWorld(&m_renderWorld);
        texts.SetResources(m_context.assets, m_context.renderer, m_context.tasks);
        // 수신은 가장 앞(50), 송신은 가장 뒤(500)다. 한 시스템이면 물리보다 앞이면서 뒤일 수 없다(network-plan §2.6).
        if (m_context.network != nullptr)
        {
            systems.AddSystem<System::NetworkReceiveSystem>(*m_context.network);
            systems.AddSystem<System::NetworkSendSystem>(*m_context.network);
        }
        // 오디오가 있으면 소스·리스너 시스템을 세운다(D-197). 없으면(끈 호스트) 소스는 읽히기만 한다.
        if (m_context.audio != nullptr)
        {
            systems.AddSystem<System::Audio2DSystem>(*m_context.audio);
        }
        // 시스템이 막 섰다. 지금 정해져 있는 값을 그대로 적용한다 - 프로젝트를 열기 전에
        // 꺼 두었으면 첫 프레임부터 꺼져 있어야 한다.
        ApplySimulationEnabled();
    }
    void Framework2D::RunFixedSteps()
    {
        // 몇 스텝을 돌지는 시계가 정했다(누산·상한·타임스케일·한 프레임 진행, D-242).
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

    void Framework2D::SetSteppedSystemsEnabled(Bool enabled)
    {
        SystemScheduler& systems = m_canvas->GetSystems();
        if (System::ScriptSystem* scripts = systems.FindSystem<System::ScriptSystem>())
        {
            scripts->SetEnabled(enabled);
        }
        if (System::Physics2DSystem* physics = systems.FindSystem<System::Physics2DSystem>())
        {
            physics->SetEnabled(enabled);
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
