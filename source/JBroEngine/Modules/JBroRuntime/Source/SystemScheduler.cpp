#include <JBro/Runtime/SystemScheduler.h>

#include <algorithm>

namespace JBro
{
    namespace
    {
        class ExecutionScope
        {
        public:
            explicit ExecutionScope(bool& executing) : m_executing(executing)
            {
                if (m_executing)
                {
                    throw std::logic_error("System scheduler callbacks cannot reenter the scheduler");
                }
                m_executing = true;
            }
            ~ExecutionScope()
            {
                m_executing = false;
            }
        private:
            bool& m_executing;
        };
    }

    void SystemScheduler::Initialize(Canvas& canvas)
    {
        if (m_initialized)
        {
            return;
        }
        SortByExecutionOrder();
        ExecutionScope scope(m_executing);
        try
        {
            for (auto& entry : m_systems)
            {
                entry.system->Initialize(canvas);
            }
        }
        catch (...)
        {
            for (std::size_t index = m_systems.Size(); index > 0; --index)
            {
                m_systems[index - 1].system->Shutdown(canvas);
            }
            throw;
        }
        m_initialized = true;
    }

    void SystemScheduler::FixedUpdate(Canvas& canvas, float fixedDeltaTime)
    {
        if (false == m_initialized)
        {
            return;
        }
        ExecutionScope scope(m_executing);
        for (auto& entry : m_systems)
        {
            entry.system->FixedUpdate(canvas, fixedDeltaTime);
        }
    }

    void SystemScheduler::Update(Canvas& canvas, float deltaTime)
    {
        if (false == m_initialized)
        {
            return;
        }
        ExecutionScope scope(m_executing);
        for (auto& entry : m_systems)
        {
            entry.system->Update(canvas, deltaTime);
        }
    }

    void SystemScheduler::Shutdown(Canvas& canvas)
    {
        if (false == m_initialized)
        {
            return;
        }
        ExecutionScope scope(m_executing);
        for (std::size_t index = m_systems.Size(); index > 0; --index)
        {
            m_systems[index - 1].system->Shutdown(canvas);
        }
        m_initialized = false;
    }

    void SystemScheduler::RemoveAllSystems(Canvas& canvas)
    {
        if (m_executing)
        {
            throw std::logic_error("Cannot remove systems during scheduler callbacks");
        }
        Shutdown(canvas);
        m_systems.Clear();
    }

    void SystemScheduler::SortByExecutionOrder()
    {
        if (m_executing || m_initialized)
        {
            throw std::logic_error("Cannot reorder an initialized or executing scheduler");
        }
        if (m_systems.Size() < 2)
        {
            return;
        }
        std::sort(m_systems.begin(), m_systems.end(),
            [](const Entry& left, const Entry& right)
        {
            const int leftOrder = left.system->GetExecutionOrder();
            const int rightOrder = right.system->GetExecutionOrder();
            return leftOrder == rightOrder
                ? left.registrationOrder < right.registrationOrder
                : leftOrder < rightOrder;
        });
    }

    std::size_t SystemScheduler::GetSystemCount() const
    {
        return m_systems.Size();
    }

    GameSystem* SystemScheduler::GetSystem(std::size_t index)
    {
        return index < m_systems.Size() ? m_systems[index].system.Get() : nullptr;
    }
}
