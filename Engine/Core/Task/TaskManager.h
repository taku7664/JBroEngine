#pragma once

#include "Core/Task/TaskGroup.h"
#include "Core/Task/TaskTypes.h"
#include "Utillity/Pointer/SafePtr.h"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class CTaskManager final : public EnableSafeFromThis<CTaskManager>
{
public:
	bool Initialize(const TaskManagerDesc& desc = {});
	void Finalize();

	SafePtr<CTaskGroup> CreateTaskGroup(const char* name);
	// description 은 UI 표시용 작업 이름. nullptr 이면 빈 문자열로 저장된다.
	SafePtr<CTask> CreateTask(const char* name, CTask::TaskFunction function, const char* description = nullptr);
	SafePtr<CTask> CreateTask(const char* name, CTask::TaskFunction function, SafePtr<CTaskGroup> group, const char* description = nullptr);

	TaskId PostMainThreadTask(std::function<void()> task);
	void DrainMainThreadCallbacks();
	bool IsWorkerThreadSupported() const;
	bool IsInitialized() const;

private:
	void QueueTask(CTask* task);
	void WorkerLoop();
	void RunTask(CTask& task);
	TaskId NextTaskId();

private:
	std::vector<OwnerPtr<CTask>> m_tasks;
	std::vector<OwnerPtr<CTaskGroup>> m_taskGroups;
	std::queue<CTask*> m_pendingTasks;
	std::queue<std::function<void()>> m_mainThreadCallbacks;
	std::vector<std::thread> m_workers;
	std::mutex m_taskMutex;
	std::mutex m_mainThreadCallbackMutex;
	std::condition_variable m_taskCondition;
	TaskId m_nextTaskId = 1;
	bool m_stopRequested = false;
	bool m_useWorkers = false;
	bool m_isInitialized = false;
};
