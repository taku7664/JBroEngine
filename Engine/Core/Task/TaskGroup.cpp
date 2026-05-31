#include "pch.h"
#include "TaskGroup.h"

#include "Core/Task/Task.h"
#include "Core/Task/TaskManager.h"

CTaskGroup::CTaskGroup(CTaskManager& manager, TaskId id, std::string name)
	: m_manager(&manager)
	, m_id(id)
	, m_name(std::move(name))
{
}

TaskId CTaskGroup::GetId() const
{
	return m_id;
}

const std::string& CTaskGroup::GetName() const
{
	return m_name;
}

SafePtr<CTask> CTaskGroup::CreateTask(const char* name, CTask::TaskFunction function, const char* description)
{
	return m_manager ? m_manager->CreateTask(name, std::move(function), SafeFromThis(), description) : nullptr;
}

std::uint32_t CTaskGroup::GetTotalTaskCount() const
{
	std::lock_guard lock(m_mutex);
	return static_cast<std::uint32_t>(m_tasks.size());
}

std::uint32_t CTaskGroup::GetCompletedTaskCount() const
{
	return m_completedTaskCount.load();
}

float CTaskGroup::GetProgress01() const
{
	const std::uint32_t total = GetTotalTaskCount();
	return 0 == total ? 1.0f : static_cast<float>(GetCompletedTaskCount()) / static_cast<float>(total);
}

bool CTaskGroup::IsCompleted() const
{
	const std::uint32_t total = GetTotalTaskCount();
	return total > 0 && GetCompletedTaskCount() >= total;
}

std::vector<TaskProgressInfo> CTaskGroup::GetTaskProgressSnapshot() const
{
	std::vector<TaskProgressInfo> snapshot;
	std::lock_guard lock(m_mutex);
	snapshot.reserve(m_tasks.size());
	for (const SafePtr<CTask>& task : m_tasks)
	{
		if (false == task.IsValid())
		{
			continue;
		}
		TaskProgressInfo info;
		info.Name        = task->GetName();
		info.Description  = task->GetDescription();
		info.Completed    = task->IsFinished();
		snapshot.push_back(std::move(info));
	}
	return snapshot;
}

void CTaskGroup::AddTask(SafePtr<CTask> task)
{
	if (task)
	{
		std::lock_guard lock(m_mutex);
		m_tasks.push_back(task);
	}
}

void CTaskGroup::NotifyTaskFinished()
{
	const std::uint32_t completed = ++m_completedTaskCount;
	const std::uint32_t total = GetTotalTaskCount();
	if (m_manager && total > 0 && completed >= total && false == m_allCompletedQueued.exchange(true))
	{
		m_manager->PostMainThreadTask([group = SafeFromThis()]()
		{
			if (group && group->AllCompletedCallback)
			{
				group->AllCompletedCallback();
			}
		});
	}
}
