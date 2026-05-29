#pragma once

#include "Core/Task/Task.h"
#include "Core/Task/TaskTypes.h"
#include "Utillity/SafePtr.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class CTask;
class CTaskManager;

class CTaskGroup final : public EnableSafeFromThis<CTaskGroup>
{
	friend class CTaskManager;

public:
	using GroupCallback = std::function<void()>;

public:
	CTaskGroup(CTaskManager& manager, TaskId id, std::string name);

	TaskId GetId() const;
	const std::string& GetName() const;
	SafePtr<CTask> CreateTask(const char* name, CTask::TaskFunction function);
	std::uint32_t GetTotalTaskCount() const;
	std::uint32_t GetCompletedTaskCount() const;
	float GetProgress01() const;
	bool IsCompleted() const;

public:
	GroupCallback AllCompletedCallback;

private:
	void AddTask(SafePtr<CTask> task);
	void NotifyTaskFinished();

private:
	CTaskManager* m_manager = nullptr;
	TaskId m_id = INVALID_TASK_ID;
	std::string m_name;
	std::vector<SafePtr<CTask>> m_tasks;
	mutable std::mutex m_mutex;
	std::atomic_uint32_t m_completedTaskCount = 0;
	std::atomic_bool m_allCompletedQueued = false;
};
