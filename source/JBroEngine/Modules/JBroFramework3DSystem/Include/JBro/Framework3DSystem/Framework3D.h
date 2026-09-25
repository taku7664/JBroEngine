#pragma once

#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Physics3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>
#include <JBro/Framework3DSystem/Rendering/RenderWorld3D.h>
#include <JBro/Framework3DSystem/System/Camera3DSystem.h>
#include <JBro/Framework3DSystem/System/MeshRender3DSystem.h>
#include <JBro/Framework3DSystem/System/Transform3DSystem.h>
#include <JBro/Host/IFramework.h>

namespace JBro
{
    // 3D 프레임워크다. 2D 와 같은 뼈대(캔버스·시스템·렌더 월드·브리지)이고, 스크립트 시스템과
    // 물리는 아직 없다(framework3d-plan §3).
    class Framework3D final : public IFramework
    {
    public:
        ~Framework3D() override;
        bool Initialize(const FrameworkContext& context) override;
        bool BindScriptContexts() noexcept override;
        void UnbindScriptContexts() noexcept override;
        void Update(float deltaTime) override;
        // 3D 에 스크립트·물리 시스템은 아직 없다. 지금 세우는 것은 소리뿐이다(D-197).
        void SetSimulationEnabled(bool enabled) override;
        RenderResult Render() override;
        RenderResult RenderEditorView(const EditorViewDesc& view) override;
        void Shutdown() override;
        void BindCanvasAssets() override;

        Canvas* GetCanvas();
        RenderWorld3D* GetRenderWorld();
        MeshLibrary& GetMeshLibrary();

    private:
        void CreateDefaultSystems();
        void RunFixedSteps(float deltaTime);

        FrameworkContext m_context;
        OwnerPtr<Canvas> m_canvas;
        // 열린 캔버스가 잡은 에셋 핸들이다. 다음 해석과 종료 때 놓는다(D-115).
        Array<AssetHandle> m_canvasAssets;
        RenderWorld3D m_renderWorld;
        MeshLibrary m_meshes;
        double m_fixedAccumulator = 0.0;
        bool m_initialized = false;
        bool m_simulationEnabled = true;
    };

    IFramework* CreateFramework3D();
    void        DestroyFramework3D(IFramework* framework);
}
