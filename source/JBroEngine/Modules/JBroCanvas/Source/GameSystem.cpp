#include <JBro/Canvas/GameSystem.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro
{
    void GameSystem::Initialize (Canvas& canvas)                          { if (m_initialized) return; OnInitialize (canvas); m_initialized = true; }
    void GameSystem::FixedUpdate(Canvas& canvas, Float fixedDeltaTime)    { if (m_enabled) OnFixedUpdate(canvas, fixedDeltaTime); }
    void GameSystem::Update     (Canvas& canvas, Float deltaTime)         { if (m_enabled) OnUpdate     (canvas, deltaTime); }
    void GameSystem::Shutdown   (Canvas& canvas)                          { if (false == m_initialized) return; OnShutdown(canvas); m_initialized = false; }

    Bool GameSystem::IsInitialized() const { return m_initialized; }
    Bool GameSystem::IsEnabled()     const { return m_enabled; }
    void GameSystem::SetEnabled(Bool enabled) { m_enabled = enabled; }
    Int32  GameSystem::GetExecutionOrder() const { return 0; }

    void GameSystem::OnInitialize (Canvas&) {}
    void GameSystem::OnFixedUpdate(Canvas&, Float) {}
    void GameSystem::OnUpdate     (Canvas&, Float) {}
    void GameSystem::OnShutdown   (Canvas&) {}
}
