#pragma once

#include <JBro/Framework2D/Internal/SystemContext.h>
#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Prefab/Prefab.h>
#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>
#include <JBro/Framework2D/Layer2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2DSystem/Scripting/ScriptSystem.h>
#include <JBro/Framework2DSystem/System/Camera2DSystem.h>
#include <JBro/Framework2DSystem/System/Physics2DSystem.h>
#include <JBro/Framework2DSystem/System/SpriteRender2DSystem.h>
#include <JBro/Framework2DSystem/System/Transform2DSystem.h>
#include <JBro/Host/IFramework.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/SystemScheduler.h>
#include <JBro/Framework2DSystem/Rendering/SpriteLibrary.h>
#include <JBro/Types/Table.h>

namespace JBro
{
    // 네트워크가 있을 때 캔버스 풀을 복제 풀로 감싼 것들. 정의는 Framework2D.cpp 에만 있다 - 이 헤더를 쓰는 에디터가
    // 네트워크 헤더를 보지 않게 하기 위해서다(D-122).
    struct Framework2DNetworkBinding;

    class Framework2D final : public IFramework
    {
    public:
        ~Framework2D() override;
        bool Initialize(const FrameworkContext& context) override;
        bool BindScriptContexts() noexcept override;
        void UnbindScriptContexts() noexcept override;
        void Update() override;
        JArrayView<ScriptContextBlock> GetScriptContextBlocks() const noexcept override;
        void SetSimulationEnabled(bool enabled) override;
        void SetScreenSpace(const ScreenSpaceFrame& frame) override;
        const ScreenSpaceFrame& GetScreenSpace() const;
        RenderResult Render() override;
        RenderResult RenderEditorView(const EditorViewDesc& view) override;
        void Shutdown() override;
        void BindCanvasAssets() override;
        std::size_t ReleaseModuleScripts() override;
        void CollectCanvasAssetIds(Array<AssetId>& ids) override;

        // 물리의 좁은 판정을 나눌 워커 수(D-223). 호스트가 프로젝트를 연 뒤 `ResolvePhysicsWorkerCount` 로 푼 값을 넘긴다.
        void           SetPhysicsWorkerCount(std::uint32_t count);
        // 물리 레이어 충돌 표를 캔버스의 물리에 넘긴다(D-233). 행 i 의 비트 j 는 레이어 i 와 j 가 서로 지나간다.
        void           SetPhysicsIgnoredLayers(const std::uint32_t (&rows)[32]);
        std::uint32_t  GetPhysicsWorkerCount();
        Canvas*        GetCanvas();
        RenderWorld2D* GetRenderWorld();
        SpriteLibrary* GetSpriteLibrary();
        Layer*         CreateLayer(const char* name = nullptr);
        bool           DestroyLayer(LayerId layer);
        bool           MoveLayer(LayerId layer, std::size_t newIndex);
        Layer2D*       GetLayer2D(LayerId layer);

    private:
        void CreateDefaultSystems();
        void RunFixedSteps();
        // 멈춘 게임의 한 프레임 진행(D-242)에서 그 프레임만 스크립트·물리를 켰다 끈다. 오디오·네트워크는 건드리지 않는다.
        void SetSteppedSystemsEnabled(bool enabled);
        // 지금 정해져 있는 값을 시스템들에 먹인다. 시스템이 선 뒤와 값이 바뀔 때 부른다.
        void ApplySimulationEnabled();

        FrameworkContext m_context;
        OwnerPtr<Canvas> m_canvas;
        // 열린 캔버스가 잡은 에셋 핸들이다. 다음 해석과 종료 때 놓는다(D-115).
        Array<AssetHandle> m_canvasAssets;
        Table<LayerId, OwnerPtr<Layer2D>> m_layer2DStates;
        RenderWorld2D    m_renderWorld;
        // 호스트가 준 화면 기준이다(D-237). 대상 크기가 0 이면(호스트가 주지 않은 시험) 렌더러의 프레임 크기를 쓴다.
        ScreenSpaceFrame m_screenSpace;
        // 스프라이트 에셋 → 렌더러 텍스처(D-113). 렌더러보다 먼저 내려가야 텍스처를 돌려줄 수 있다.
        SpriteLibrary    m_spriteLibrary;
        // 복제 풀 어댑터들(D-122). 네트워크가 있을 때만 있고, 캔버스와 함께 죽는다.
        OwnerPtr<Framework2DNetworkBinding> m_networkBinding;
        // 스크립트 DLL 에 넘길 블록과 그 실체다. 블록이 이것들을 가리키므로
        // 프레임워크보다 먼저 죽으면 안 된다.
        Framework2DSystemContext  m_scriptSystems;
        Framework2DServiceContext m_scriptServices;
        ScriptContextBlock        m_scriptBlocks[2];
        std::uint32_t             m_scriptBlockCount = 0;
        bool             m_initialized      = false;
        // 게임을 돌릴 것인가(D-131). 게임 실행은 손대지 않으므로 기본이 참이다.
        bool             m_simulationEnabled = true;
    };

    IFramework* CreateFramework2D();
    void        DestroyFramework2D(IFramework* framework);
}
