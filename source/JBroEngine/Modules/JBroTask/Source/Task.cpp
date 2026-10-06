#include <JBro/Task/Task.h>

#include <utility>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    Task::Task(String name, UInt32 numSubTasks)
        : m_name(std::move(name))
        , m_numSubTasks(numSubTasks)
    {
    }

    Task::~Task() = default;

    const String& Task::GetName() const
    {
        return m_name;
    }

    UInt32 Task::GetNumSubTasks() const
    {
        return m_numSubTasks;
    }

    UInt32 Task::GetSucceededSubTasks() const
    {
        return m_succeededSubTasks.load(std::memory_order_acquire);
    }

    UInt32 Task::GetFailedSubTasks() const
    {
        return m_failedSubTasks.load(std::memory_order_acquire);
    }

    TaskState Task::GetState() const
    {
        return m_state.load(std::memory_order_acquire);
    }

    Bool Task::IsFinished() const
    {
        const TaskState state = GetState();
        return state == TaskState::Completed || state == TaskState::Failed || state == TaskState::Canceled;
    }

    void Task::RequestCancel()
    {
        m_cancelRequested.store(true, std::memory_order_release);
    }

    Bool Task::IsCancelRequested() const
    {
        return m_cancelRequested.load(std::memory_order_acquire);
    }

    TaskResult Task::GetResult() const
    {
        TaskResult result;
        result.state = GetState();
        result.numSubTasks = m_numSubTasks;
        result.succeededSubTasks = GetSucceededSubTasks();
        result.failedSubTasks = GetFailedSubTasks();
        result.failures = m_failures.View();
        return result;
    }

    void Task::OnFinished(const TaskResult&)
    {
    }

    Bool Task::ClaimSubTask()
    {
        std::uint32_t reported = m_reportedSubTasks.load(std::memory_order_relaxed);
        while (reported < m_numSubTasks)
        {
            if (m_reportedSubTasks.compare_exchange_weak(reported, reported + 1, std::memory_order_relaxed))
            {
                return true;
            }
        }
        return false;
    }

    Bool Task::SucceedSubTask()
    {
        if (false == ClaimSubTask())
        {
            return false;
        }
        m_succeededSubTasks.fetch_add(1, std::memory_order_release);
        return true;
    }

    Bool Task::FailSubTask(UInt32 subTask, const char* reason)
    {
        if (false == ClaimSubTask())
        {
            return false;
        }
        TaskFailure& failure = m_failures.Emplace();
        failure.subTask = subTask;
        failure.reason = reason != nullptr ? reason : "";
        m_failedSubTasks.fetch_add(1, std::memory_order_release);
        return true;
    }
}
