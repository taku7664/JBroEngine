#include <JBro/Task/TaskGroup.h>

#include <utility>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    TaskGroup::TaskGroup(String name, TaskGroupOrder order)
        : m_name(std::move(name))
        , m_order(order)
    {
    }

    TaskGroup::~TaskGroup() = default;

    Bool TaskGroup::Add(OwnerPtr<Task> task)
    {
        if (m_submitted || task.Get() == nullptr)
        {
            return false;
        }
        m_tasks.Add(std::move(task));
        return true;
    }

    const String& TaskGroup::GetName() const
    {
        return m_name;
    }

    TaskGroupOrder TaskGroup::GetOrder() const
    {
        return m_order;
    }

    TaskGroupId TaskGroup::GetId() const
    {
        return m_id;
    }

    UInt32 TaskGroup::GetTaskCount() const
    {
        return static_cast<std::uint32_t>(m_tasks.Size());
    }

    const Task& TaskGroup::GetTaskAt(UInt32 index) const
    {
        return *m_tasks[index];
    }

    Bool TaskGroup::IsSubmitted() const
    {
        return m_submitted;
    }

    Bool TaskGroup::IsFinished() const
    {
        return m_finished;
    }

    TaskState TaskGroup::GetState() const
    {
        if (false == m_finished)
        {
            for (const OwnerPtr<Task>& task : m_tasks)
            {
                if (task->GetState() != TaskState::Pending)
                {
                    return TaskState::Running;
                }
            }
            return TaskState::Pending;
        }
        Bool canceled = false;
        for (const OwnerPtr<Task>& task : m_tasks)
        {
            const TaskState state = task->GetState();
            if (state == TaskState::Failed)
            {
                return TaskState::Failed;
            }
            if (state == TaskState::Canceled)
            {
                canceled = true;
            }
        }
        return canceled ? TaskState::Canceled : TaskState::Completed;
    }

    void TaskGroup::RequestCancel()
    {
        for (OwnerPtr<Task>& task : m_tasks)
        {
            task->RequestCancel();
        }
    }

    void TaskGroup::OnFinished()
    {
    }
}
