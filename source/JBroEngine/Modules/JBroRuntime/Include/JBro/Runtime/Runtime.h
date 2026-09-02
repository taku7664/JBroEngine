#pragma once

#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/GameSystem.h>
#include <JBro/Runtime/SystemScheduler.h>
#include <JBro/Runtime/World.h>

namespace JBro::Engine
{
    class RuntimeModule final : public IModule
    {
    public:
        bool Initialize(const JMemoryContext& memory) override;
        void Shutdown() override;

    };
}
