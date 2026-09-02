#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/Graphics/Graphics.h>
#include <JBro/Runtime/Runtime.h>

namespace JBro
{
    struct FrameworkContext
    {
        JMemoryContext memory;
        AssetManager* assets = nullptr;
        GraphicsSystem* graphics = nullptr;
    };

    class IFramework
    {
    public:
        virtual ~IFramework() = default;

        virtual bool Initialize(const FrameworkContext& context) = 0;
        virtual void Update(float deltaTime) = 0;
        virtual void Shutdown() = 0;
    };
}
