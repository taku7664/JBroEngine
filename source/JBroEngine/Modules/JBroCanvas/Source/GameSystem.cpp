#include <JBro/Canvas/GameSystem.h>

namespace JBro
{
    void GameSystem::Initialize (Canvas& canvas)                          { if (m_initialized) return; OnInitialize (canvas); m_initialized = true; }
    void GameSystem::FixedUpdate(Canvas& canvas, float fixedDeltaTime)    { if (m_enabled) OnFixedUpdate(canvas, fixedDeltaTime); }
    void GameSystem::Update     (Canvas& canvas, float deltaTime)         { if (m_enabled) OnUpdate     (canvas, deltaTime); }
    void GameSystem::Shutdown   (Canvas& canvas)                          { if (false == m_initialized) return; OnShutdown(canvas); m_initialized = false; }

    bool GameSystem::IsInitialized() const { return m_initialized; }
    bool GameSystem::IsEnabled()     const { return m_enabled; }
    void GameSystem::SetEnabled(bool enabled) { m_enabled = enabled; }
    int  GameSystem::GetExecutionOrder() const { return 0; }

    void GameSystem::OnInitialize (Canvas&) {}
    void GameSystem::OnFixedUpdate(Canvas&, float) {}
    void GameSystem::OnUpdate     (Canvas&, float) {}
    void GameSystem::OnShutdown   (Canvas&) {}
}
