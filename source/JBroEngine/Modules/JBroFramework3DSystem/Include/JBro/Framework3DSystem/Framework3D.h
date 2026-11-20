#pragma once

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/CanvasObjectSystem.h>
#include <JBro/Canvas/CanvasViewportSystem.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Physics3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3D/Internal/SystemContext.h>
#include <JBro/Framework3D/ServiceContext.h>
#include <JBro/Framework3DSystem/Rendering/MeshMaterialLibrary.h>
#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>
#include <JBro/Graphics/MaterialLibrary.h>
#include <JBro/Graphics/PostProcessLibrary.h>
#include <JBro/Graphics/PostProcessMixer.h>
#include <JBro/Host/MaterialSystem.h>
#include <JBro/Host/PostProcessSystem.h>
#include <JBro/Framework3DSystem/Rendering/RenderWorld3D.h>
#include <JBro/Framework3DSystem/System/Camera3DSystem.h>
#include <JBro/Framework3DSystem/System/MeshRender3DSystem.h>
#include <JBro/Framework3DSystem/System/Transform3DSystem.h>
#include <JBro/Host/IFramework.h>
#include <JBro/ScreenUi/Rendering/RenderWorld2D.h>
#include <JBro/ScreenUi/Rendering/SpriteLibrary.h>
#include <JBro/Framework3DSystem/Scripting/UiBindings3D.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // 3D 프레임워크다. 2D 와 같은 뼈대(캔버스·시스템·렌더 월드·브리지)이고, 물리는 아직 없다(framework3d-plan §3).
    // 스크립트는 2D 와 같은 `ScriptSystem`(공용 모듈 `JBroScriptRuntime`)이 돌린다(D-363).
    // **화면 레이어는 화면 UI 다**(D-362). `Space = Screen` 레이어의 2D 컴포넌트(트랜스폼·스프라이트·텍스트·위젯)를 공용 모듈 `JBroScreenUi` 의
    // 시스템이 제 렌더 월드(`m_screenWorld`)에 담고, 3D 뷰를 다 그린 뒤 화면 뷰로 얹는다. 위젯은 입력 체인 대신 이 프레임워크가 입력을 직접 넘긴다.
    class Framework3D final : public IFramework
    {
    public:
        ~Framework3D() override;
        Bool Initialize(const FrameworkContext& context) override;
        Bool BindScriptContexts() noexcept override;
        void UnbindScriptContexts() noexcept override;
        JArrayView<ScriptContextBlock> GetScriptContextBlocks() const noexcept override;
        void Update() override;
        // 멈추면 스크립트와 소리를 세운다(D-197·D-363). 물리는 3D 에 아직 없다.
        void SetSimulationEnabled(Bool enabled) override;
        void PrepareBreakFrames() override;
        void EndBreakFrames() override;
        RenderResult Render() override;
        RenderResult RenderEditorView(const EditorViewDesc& view) override;
        RenderResult RenderLayerThumbnail(const LayerThumbnailDesc& thumbnail) override;
        void SetScreenSpace(const ScreenSpaceFrame& frame) override;
        void Shutdown() override;
        void BindCanvasAssets() override;
        std::size_t ReleaseModuleScripts() override;
        void CollectCanvasAssetIds(Array<AssetId>& ids) override;

        Canvas* GetCanvas();
        RenderWorld3D* GetRenderWorld();
        MeshLibrary& GetMeshLibrary();
        // 3D 물리의 워커 수(D-419). 호스트가 프로젝트를 연 뒤 `ResolvePhysicsWorkerCount3D` 로 푼 값을 넘긴다(2D 의 `Framework2D` 와 같은 자리).
        void SetPhysicsWorkerCount(UInt32 count);
        // 3D 물리의 충돌 스텝(D-436). 호스트가 프로젝트의 `build.physicsCollisionSteps3D` 를 넘긴다.
        void SetPhysicsCollisionSteps(UInt32 steps);
        // 레이어 충돌 표(D-421). 호스트가 `ResolvePhysicsIgnoredLayers` 로 푼 표를 넘긴다(2D 와 같다).
        void SetPhysicsIgnoredLayers(const UInt32 (&rows)[32]);
        // 화면 레이어의 그릴 것이다(D-362).
        RenderWorld2D* GetScreenRenderWorld();
        // 표준 머티리얼(D-399)을 푸는 곳이다. 셰이더 머티리얼(D-347)은 `m_materials` 다.
        MeshMaterialLibrary& GetMeshMaterialLibrary();

    private:
        void CreateDefaultSystems();
        void RunFixedSteps();
        // 시뮬레이션에 따라 스크립트를 켜고 끈다(소리는 `SetSimulationEnabled` 가 따로 다룬다). 한 프레임 진행은 그 프레임만 켠다.
        void ApplySimulationEnabled(Bool enabled);
        // 카메라의 프로필과 볼륨·덮어쓰기를 섞고 레이어 프로필을 건다(D-344~D-346). 추출 뒤에 부른다.
        void ResolvePostProcess();
        // 카메라 하나의 프로필에 그 자리의 볼륨을 섞고 덮어쓰기를 얹는다(D-346). `overridesFrom` 이 있으면 그 섞기의 덮어쓰기를 옮겨 온다(D-438, 둘째 뷰부터).
        void MixPostProcessFor(const RenderCamera3D& camera, PostProcessMixer& mixer, const PostProcessMixer* overridesFrom);

        FrameworkContext m_context;
        OwnerPtr<Canvas> m_canvas;
        // 열린 캔버스가 잡은 에셋 핸들이다. 다음 해석과 종료 때 놓는다(D-115).
        Array<AssetHandle> m_canvasAssets;
        RenderWorld3D m_renderWorld;
        // 화면 레이어의 스프라이트·글자다(D-362). 월드 레이어의 아이템은 담기지만 그리지 않는다 - 3D 캔버스의 2D 컴포넌트는 화면 레이어에만 둔다.
        RenderWorld2D m_screenWorld;
        SpriteLibrary m_sprites;
        // 화면 UI 가 3D 스크립트의 훅을 부르는 자리다(D-363).
        System::UiHookDispatcher3D m_uiHooks;
        // 호스트가 프레임마다 거는 화면 기준이다(D-237).
        ScreenSpaceFrame m_screenSpace;
        MeshLibrary m_meshes;
        // 후처리 효과 `Material` 의 머티리얼을 푼다(D-347). 메시의 머티리얼은 아직 보지 않는다.
        MaterialLibrary m_materials;
        // 카메라의 후처리 프로필을 푼다(D-344).
        PostProcessLibrary m_postProcess;
        // 카메라의 프로필에 볼륨을 섞고 스크립트의 덮어쓰기를 얹는다(D-346). 2D 와 같다.
        PostProcessMixer m_postMixer;
        // 둘째 뷰부터의 섞기(D-438, 3D 여러 뷰). 주 카메라는 `m_postMixer` 다. 자리는 처음에 `MaxCameraMixers` 만큼 잡는다.
        Array<PostProcessMixer> m_cameraMixers;
        static constexpr UInt32 MaxCameraMixers = 8;
        PostProcessSystem m_postProcessSystem;
        // 스크립트의 머티리얼 파라미터 덮어쓰기(D-348). `m_materials` 에 묶인다.
        MaterialSystem m_materialSystem;
        struct VolumeSample
        {
            Int32 priority = 0;
            Float weight = 0.0f;
            AssetHandle profile;
        };
        Array<VolumeSample> m_volumes;
        static constexpr UInt32 PostMixerEntries = 32;
        static constexpr UInt32 PostMixerBlockBytes = 4096;
        static constexpr UInt32 MaxVolumes = 64;
        // 레이어 효과 자리의 처음 크기다(레이어 수). 넘으면 그때 늘린다.
        static constexpr UInt32 LayerPostProcessCapacity = 64;
        // 표준 머티리얼(D-399)·모델의 재질을 렌더러에 올린다. 셰이더 머티리얼은 위의 `m_materials` 다.
        MeshMaterialLibrary m_meshMaterials;
        Bool m_initialized = false;
        Bool m_simulationEnabled = true;
        // 스크립트에 건네는 3D 컨텍스트다(D-224). 2D 의 `Framework2D` 와 같은 자리다.
        Framework3DSystemContext m_scriptSystems;
        Framework3DServiceContext m_scriptServices;
        ScriptContextBlock m_scriptBlocks[2] = {};
        UInt32 m_scriptBlockCount = 0;
        // 스크립트의 오브젝트 만들기·찾기가 이 캔버스에 닿는 길이다(D-301). 스크립트 컨텍스트와 같은 수명이다.
        CanvasObjectSystem m_objects;
        // 스크립트의 뷰포트 서비스(D-438).
        CanvasViewportSystem m_viewports;
    };

    IFramework* CreateFramework3D();
    void        DestroyFramework3D(IFramework* framework);
}
