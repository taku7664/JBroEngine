#pragma once

#include <cstdint>
#include <string>

using TaskId = std::uint64_t;

inline constexpr TaskId INVALID_TASK_ID = 0;

enum class ETaskState
{
	Pending,
	Running,
	Completed,
	Failed,
	Canceled
};

struct TaskManagerDesc
{
	std::uint32_t WorkerCount = 0;
	bool EnableWorkers = true;
};

// UI 진행률 표시용 태스크 스냅샷 — CTaskGroup::GetTaskProgressSnapshot 가 반환.
struct TaskProgressInfo
{
	std::string Name;
	std::string Description;
	bool        Completed = false;
};
