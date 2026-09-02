#pragma once

#include <JBro/Framework2D/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Prefab/Prefab.h>
#include <JBro/Framework2D/Rendering/RenderWorld2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2D/Scripting/ScriptSystem.h>
#include <JBro/Framework2D/System/CameraSystem2D.h>
#include <JBro/Framework2D/System/Physics2DSystem.h>
#include <JBro/Framework2D/System/SpriteRenderSystem.h>
#include <JBro/Framework2D/System/TransformSystem2D.h>
#include <JBro/Runtime/IFramework.h>
#include <JBro/Runtime/SystemScheduler.h>

namespace JBro
{
    class Framework2D final : public IFramework
    {
    public:
        bool Initialize(const FrameworkContext& context) override;
        void Update(float deltaTime) override;
        void Shutdown() override;

        Canvas*        GetCanvas();
        RenderWorld2D* GetRenderWorld();

    private:
        void CreateDefaultSystems();
        void RunFixedSteps(float deltaTime);

        FrameworkContext m_context;
        OwnerPtr<Canvas> m_canvas;
        SystemScheduler  m_systems;
        RenderWorld2D    m_renderWorld;
        float            m_fixedAccumulator = 0.0f;
        float            m_fixedDeltaTime   = 1.0f / 60.0f;
        bool             m_initialized      = false;
    };

    IFramework* CreateFramework2D();
    void        DestroyFramework2D(IFramework* framework);
}
