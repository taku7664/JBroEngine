#include <JBro/Runtime/SystemScheduler.h>

#include <algorithm>

namespace JBro
{
    void SystemScheduler::Initialize(Canvas& canvas)
    {
        if (m_initialized) return;
        SortByExecutionOrder();
        for (auto& system : m_systems) system->Initialize(canvas);
        m_initialized = true;
    }
    void SystemScheduler::FixedUpdate(Canvas& canvas, float fixedDeltaTime) { for (auto& s : m_systems) s->FixedUpdate(canvas, fixedDeltaTime); }
    void SystemScheduler::Update     (Canvas& canvas, float deltaTime)      { for (auto& s : m_systems) s->Update     (canvas, deltaTime); }
    void SystemScheduler::Shutdown   (Canvas& canvas)
    {
        for (auto it = m_systems.rbegin(); it != m_systems.rend(); ++it) (*it)->Shutdown(canvas);
        m_initialized = false;
    }
    void SystemScheduler::RemoveAllSystems(Canvas& canvas)
    {
        Shutdown(canvas);
        m_systems.clear();
    }
    void SystemScheduler::SortByExecutionOrder()
    {
        std::sort(m_systems.begin(), m_systems.end(),
            [](const auto& a, const auto& b) { return a->GetExecutionOrder() < b->GetExecutionOrder(); });
    }
    std::size_t SystemScheduler::GetSystemCount() const { return m_systems.size(); }
    GameSystem* SystemScheduler::GetSystem(std::size_t index) { return index < m_systems.size() ? m_systems[index].get() : nullptr; }
}
