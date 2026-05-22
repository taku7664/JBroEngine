#include "pch.h"
#include "ThreadService.h"

bool CThreadService::Initialize()
{
	m_isInitialized = true;
	return true;
}

void CThreadService::Finalize()
{
	std::lock_guard<std::mutex> lock(m_mainThreadTaskMutex);
	while (false == m_mainThreadTasks.empty())
	{
		m_mainThreadTasks.pop();
	}
	m_isInitialized = false;
}

ThreadTaskId CThreadService::PostTask(std::function<void()> task)
{
	if (false == m_isInitialized || false == static_cast<bool>(task))
	{
		return INVALID_THREAD_TASK_ID;
	}

	std::lock_guard<std::mutex> lock(m_mainThreadTaskMutex);
	const ThreadTaskId taskId = m_nextTaskId++;
	m_mainThreadTasks.push(std::move(task));
	return taskId;
}

void CThreadService::ExecuteMainThreadTasks()
{
	std::queue<std::function<void()>> tasks;
	{
		std::lock_guard<std::mutex> lock(m_mainThreadTaskMutex);
		tasks.swap(m_mainThreadTasks);
	}

	while (false == tasks.empty())
	{
		std::function<void()> task = std::move(tasks.front());
		tasks.pop();
		if (task)
		{
			task();
		}
	}
}

bool CThreadService::IsMultithreaded() const
{
#if JBRO_PLATFORM_WEB
	return false;
#else
	return true;
#endif
}
