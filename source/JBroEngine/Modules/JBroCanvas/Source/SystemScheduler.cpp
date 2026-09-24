#include <JBro/Canvas/SystemScheduler.h>

#include <JBro/Core/Profiler.h>

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
            // 고정 스텝도 같은 이름 아래 쌓인다(기존도 풀의 총 순회시간을 합해 냈다).
            // 부른 횟수가 함께 보이므로 한 프레임에 몇 번 돌았는지는 거기서 읽는다.
            const ProfileScope timing(entry.profileName);
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
            // **어느 시스템이 느린지 보이게 한다**(D-194). 꺼져 있으면 재는 일 자체가
            // 아무것도 하지 않으므로 게임 실행에 값을 물리지 않는다.
            //
            // **스스로 닫는 구간으로 잰다**(D-195). `Push`·`Pop` 을 손으로 짝지으면
            // 시스템이 던질 때 `Pop` 을 건너뛰고, 그 프레임부터 겹이 0 으로 돌아오지
            // 않아 그 뒤의 모든 구간이 엉뚱한 깊이로 쌓인다. `Initialize` 가 이미
            // 시스템이 던지는 것을 전제로 쓰여 있다.
            const ProfileScope timing(entry.profileName);
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
