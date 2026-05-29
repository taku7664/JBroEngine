#include "pch.h"
#include "TaskManager.h"

#include "Core/Task/Task.h"

#include <exception>

bool CTaskManager::Initialize(const TaskManagerDesc& desc)
{
	if (m_isInitialized)
	{
		return true;
	}

#if JBRO_PLATFORM_WEB
	m_useWorkers = false;
#else
	m_useWorkers = desc.EnableWorkers;
#endif
	m_stopRequested = false;
	m_isInitialized = true;

	if (m_useWorkers)
	{
		std::uint32_t workerCount = desc.WorkerCount;
		if (0 == workerCount)
		{
			workerCount = std::max<std::uint32_t>(1, std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1);
		}

		m_workers.reserve(workerCount);
		for (std::uint32_t i = 0; i < workerCount; ++i)
		{
			m_workers.emplace_back([this]() { WorkerLoop(); });
		}
	}

	return true;
}

void CTaskManager::Finalize()
{
	{
		std::lock_guard lock(m_taskMutex);
		m_stopRequested = true;
	}
	m_taskCondition.notify_all();

	for (std::thread& worker : m_workers)
	{
		if (worker.joinable())
		{
			worker.join();
		}
	}
	m_workers.clear();

	DrainMainThreadCallbacks();

	{
		std::lock_guard lock(m_taskMutex);
		while (false == m_pendingTasks.empty())
		{
			m_pendingTasks.pop();
		}
		m_tasks.clear();
		m_taskGroups.clear();
	}

	{
		std::lock_guard lock(m_mainThreadCallbackMutex);
		while (false == m_mainThreadCallbacks.empty())
		{
			m_mainThreadCallbacks.pop();
		}
	}

	m_useWorkers = false;
	m_isInitialized = false;
}

SafePtr<CTaskGroup> CTaskManager::CreateTaskGroup(const char* name)
{
	if (false == m_isInitialized)
	{
		return nullptr;
	}

	std::string taskGroupName = (nullptr != name && '\0' != name[0]) ? name : "TaskGroup";
	OwnerPtr<CTaskGroup> group = MakeOwnerPtr<CTaskGroup>(*this, NextTaskId(), std::move(taskGroupName));
	SafePtr<CTaskGroup> safeGroup = group.GetSafePtr();
	{
		std::lock_guard lock(m_taskMutex);
		m_taskGroups.push_back(std::move(group));
	}
	return safeGroup;
}

SafePtr<CTask> CTaskManager::CreateTask(const char* name, CTask::TaskFunction function)
{
	return CreateTask(name, std::move(function), nullptr);
}

SafePtr<CTask> CTaskManager::CreateTask(const char* name, CTask::TaskFunction function, SafePtr<CTaskGroup> group)
{
	if (false == m_isInitialized || false == static_cast<bool>(function))
	{
		return nullptr;
	}

	std::string taskName = (nullptr != name && '\0' != name[0]) ? name : "Task";
	OwnerPtr<CTask> task = MakeOwnerPtr<CTask>(NextTaskId(), std::move(taskName), std::move(function));
	task->SetGroup(group);
	SafePtr<CTask> safeTask = task.GetSafePtr();
	CTask* rawTask = task.Get();

	{
		std::lock_guard lock(m_taskMutex);
		m_tasks.push_back(std::move(task));
		if (group)
		{
			group->AddTask(safeTask);
		}
	}

	QueueTask(rawTask);
	return safeTask;
}

TaskId CTaskManager::PostMainThreadTask(std::function<void()> task)
{
	if (false == m_isInitialized || false == static_cast<bool>(task))
	{
		return INVALID_TASK_ID;
	}

	std::lock_guard lock(m_mainThreadCallbackMutex);
	const TaskId taskId = NextTaskId();
	m_mainThreadCallbacks.push(std::move(task));
	return taskId;
}

void CTaskManager::DrainMainThreadCallbacks()
{
	std::queue<std::function<void()>> callbacks;
	{
		std::lock_guard lock(m_mainThreadCallbackMutex);
		callbacks.swap(m_mainThreadCallbacks);
	}

	while (false == callbacks.empty())
	{
		std::function<void()> callback = std::move(callbacks.front());
		callbacks.pop();
		if (callback)
		{
			callback();
		}
	}
}

bool CTaskManager::IsWorkerThreadSupported() const
{
	return m_useWorkers;
}

bool CTaskManager::IsInitialized() const
{
	return m_isInitialized;
}

void CTaskManager::QueueTask(CTask* task)
{
	if (nullptr == task)
	{
		return;
	}

	if (m_useWorkers)
	{
		{
			std::lock_guard lock(m_taskMutex);
			m_pendingTasks.push(task);
		}
		m_taskCondition.notify_one();
		return;
	}

	RunTask(*task);
}

void CTaskManager::WorkerLoop()
{
	while (true)
	{
		CTask* task = nullptr;
		{
			std::unique_lock lock(m_taskMutex);
			m_taskCondition.wait(lock, [this]()
			{
				return m_stopRequested || false == m_pendingTasks.empty();
			});

			if (m_stopRequested && m_pendingTasks.empty())
			{
				return;
			}

			task = m_pendingTasks.front();
			m_pendingTasks.pop();
		}

		if (task)
		{
			RunTask(*task);
		}
	}
}

void CTaskManager::RunTask(CTask& task)
{
	if (task.IsCancelRequested())
	{
		task.SetState(ETaskState::Canceled);
	}
	else
	{
		task.SetState(ETaskState::Running);
		try
		{
			CTask::TaskFunction function = task.TakeFunction();
			if (function)
			{
				function();
			}
			task.SetState(task.IsCancelRequested() ? ETaskState::Canceled : ETaskState::Completed);
		}
		catch (const std::exception& e)
		{
			task.SetErrorMessage(e.what());
			task.SetState(ETaskState::Failed);
		}
		catch (...)
		{
			task.SetErrorMessage("Unknown task exception.");
			task.SetState(ETaskState::Failed);
		}
	}

	PostMainThreadTask([safeTask = task.SafeFromThis()]()
	{
		if (safeTask && safeTask->EndCallback)
		{
			safeTask->EndCallback();
		}

		if (SafePtr<CTaskGroup> group = safeTask ? safeTask->GetGroup() : nullptr)
		{
			group->NotifyTaskFinished();
		}
	});
}

TaskId CTaskManager::NextTaskId()
{
	std::lock_guard lock(m_taskMutex);
	return m_nextTaskId++;
}
