#pragma once

#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Physics3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Host/IFramework.h>

namespace JBro
{
    class Framework3D final : public IFramework
    {
    public:
        ~Framework3D() override;
        bool Initialize(const FrameworkContext& context) override;
        bool BindScriptContexts() noexcept override;
        void UnbindScriptContexts() noexcept override;
        void Update(float deltaTime) override;
        RenderResult Render() override;
        void Shutdown() override;

        Canvas* GetCanvas();

    private:
        void RunFixedSteps(float deltaTime);

        FrameworkContext m_context;
        OwnerPtr<Canvas> m_canvas;
        double m_fixedAccumulator = 0.0;
        bool m_initialized = false;
    };

    IFramework* CreateFramework3D();
    void        DestroyFramework3D(IFramework* framework);
}
