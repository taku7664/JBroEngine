#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/GameSystem.h>
#include <JBro/Runtime/SystemScheduler.h>

namespace JBro
{
    class RuntimeModule final : public IModule
    {
    public:
        bool Initialize(const JMemoryContext& memory) override;
        void Shutdown() override;
    };
}
