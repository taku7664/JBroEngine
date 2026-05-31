#pragma once

#include "Core/Task/TaskTypes.h"
#include "Utillity/SafePtr.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

class CTaskGroup;

class CTask final : public EnableSafeFromThis<CTask>
{
	friend class CTaskManager;

public:
	using TaskFunction = std::function<void()>;
	using TaskCallback = std::function<void()>;

public:
	CTask(TaskId id, std::string name, TaskFunction function, std::string description = {});

	TaskId GetId() const;
	const std::string& GetName() const;
	const std::string& GetDescription() const;
	ETaskState GetState() const;
	bool IsFinished() const;
	bool IsCancelRequested() const;
	void RequestCancel();
	std::string GetErrorMessage() const;

public:
	TaskCallback EndCallback;

private:
	void SetGroup(SafePtr<CTaskGroup> group);
	SafePtr<CTaskGroup> GetGroup() const;
	TaskFunction TakeFunction();
	void SetState(ETaskState state);
	void SetErrorMessage(std::string message);

private:
	TaskId m_id = INVALID_TASK_ID;
	std::string m_name;
	std::string m_description;
	TaskFunction m_function;
	SafePtr<CTaskGroup> m_group;
	std::atomic<ETaskState> m_state = ETaskState::Pending;
	std::atomic_bool m_cancelRequested = false;
	mutable std::mutex m_mutex;
	std::string m_errorMessage;
};
