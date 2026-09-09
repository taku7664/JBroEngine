#pragma once

#include <JBro/Core/Core.h>

namespace JBro
{
    class AssetManager;
    class Renderer;

    struct FrameworkContext
    {
        JMemoryContext memory;
        AssetManager* assets = nullptr;
        Renderer* renderer = nullptr;
        float fixedDeltaTime = 1.0f / 60.0f;
        std::uint32_t maxFixedStepsPerFrame = 4;
    };

    class IFramework
    {
    public:
        virtual ~IFramework() = default;

        virtual bool Initialize(const FrameworkContext& context) = 0;
        // Host activates only its successfully opened project, never standalone previews.
        // Hooks must not throw; unbinding precedes destruction of borrowed systems.
        virtual bool BindScriptContexts() noexcept = 0;
        virtual void UnbindScriptContexts() noexcept = 0;
        virtual void Update(float deltaTime) = 0;
        // Host opens/closes the Renderer frame. Framework submits its views and packets only.
        virtual bool Render() = 0;
        virtual void Shutdown() = 0;
    };
}
