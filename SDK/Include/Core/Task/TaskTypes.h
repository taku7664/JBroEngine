#pragma once

#include <cstdint>

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
