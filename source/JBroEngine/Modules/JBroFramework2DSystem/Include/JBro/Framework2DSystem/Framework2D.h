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
    class Framework2D final : public IFramework
    {
    public:
        ~Framework2D() override;
        bool Initialize(const FrameworkContext& context) override;
        bool BindScriptContexts() noexcept override;
        void UnbindScriptContexts() noexcept override;
        void Update(float deltaTime) override;
        JArrayView<ScriptContextBlock> GetScriptContextBlocks() const noexcept override;
        RenderResult Render() override;
        void Shutdown() override;

        Canvas*        GetCanvas();
        RenderWorld2D* GetRenderWorld();
        SpriteLibrary* GetSpriteLibrary();
        Layer*         CreateLayer(const char* name = nullptr);
        bool           DestroyLayer(LayerId layer);
        bool           MoveLayer(LayerId layer, std::size_t newIndex);
        Layer2D*       GetLayer2D(LayerId layer);

    private:
        void CreateDefaultSystems();
        void RunFixedSteps(float deltaTime);

        FrameworkContext m_context;
        OwnerPtr<Canvas> m_canvas;
        Table<LayerId, OwnerPtr<Layer2D>> m_layer2DStates;
        RenderWorld2D    m_renderWorld;
        // 스프라이트 에셋 → 렌더러 텍스처(D-113). 렌더러보다 먼저 내려가야 텍스처를 돌려줄 수 있다.
        SpriteLibrary    m_spriteLibrary;
        // 스크립트 DLL 에 넘길 블록과 그 실체다. 블록이 이것들을 가리키므로
        // 프레임워크보다 먼저 죽으면 안 된다.
        Framework2DSystemContext  m_scriptSystems;
        Framework2DServiceContext m_scriptServices;
        ScriptContextBlock        m_scriptBlocks[2];
        std::uint32_t             m_scriptBlockCount = 0;
        double           m_fixedAccumulator = 0.0;
        bool             m_initialized      = false;
    };

    IFramework* CreateFramework2D();
    void        DestroyFramework2D(IFramework* framework);
}
