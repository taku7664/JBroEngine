#pragma once

#include "Core/Thread/ThreadTypes.h"
#include "Utillity/SafePtr.h"

#include <functional>
#include <mutex>
#include <queue>

class CThreadService final : public EnableSafeFromThis<CThreadService>
{
public:
	bool Initialize();
	void Finalize();
	ThreadTaskId PostTask(std::function<void()> task);
	void ExecuteMainThreadTasks();
	bool IsMultithreaded() const;

private:
	std::queue<std::function<void()>> m_mainThreadTasks;
	mutable std::mutex m_mainThreadTaskMutex;
	ThreadTaskId m_nextTaskId = 1;
	bool m_isInitialized = false;
};
