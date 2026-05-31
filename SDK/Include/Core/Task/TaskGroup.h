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
	// description 은 UI 표시용 작업 이름. nullptr 이면 빈 문자열로 저장된다.
	SafePtr<CTask> CreateTask(const char* name, CTask::TaskFunction function, const char* description = nullptr);
	std::uint32_t GetTotalTaskCount() const;
	std::uint32_t GetCompletedTaskCount() const;
	float GetProgress01() const;
	bool IsCompleted() const;
	// 현재 그룹에 속한 각 태스크의 (이름/설명/완료여부) 스냅샷. UI 진행률 목록 표시용.
	std::vector<TaskProgressInfo> GetTaskProgressSnapshot() const;

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
