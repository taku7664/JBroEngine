#include "pch.h"
#include "Task.h"

CTask::CTask(TaskId id, std::string name, TaskFunction function, std::string description)
	: m_id(id)
	, m_name(std::move(name))
	, m_description(std::move(description))
	, m_function(std::move(function))
{
}

TaskId CTask::GetId() const
{
	return m_id;
}

const std::string& CTask::GetName() const
{
	return m_name;
}

const std::string& CTask::GetDescription() const
{
	return m_description;
}

ETaskState CTask::GetState() const
{
	return m_state.load();
}

bool CTask::IsFinished() const
{
	const ETaskState state = GetState();
	return ETaskState::Completed == state || ETaskState::Failed == state || ETaskState::Canceled == state;
}

bool CTask::IsCancelRequested() const
{
	return m_cancelRequested.load();
}

void CTask::RequestCancel()
{
	m_cancelRequested.store(true);
}

std::string CTask::GetErrorMessage() const
{
	std::lock_guard lock(m_mutex);
	return m_errorMessage;
}

void CTask::SetGroup(SafePtr<CTaskGroup> group)
{
	std::lock_guard lock(m_mutex);
	m_group = group;
}

SafePtr<CTaskGroup> CTask::GetGroup() const
{
	std::lock_guard lock(m_mutex);
	return m_group;
}

CTask::TaskFunction CTask::TakeFunction()
{
	std::lock_guard lock(m_mutex);
	return std::move(m_function);
}

void CTask::SetState(ETaskState state)
{
	m_state.store(state);
}

void CTask::SetErrorMessage(std::string message)
{
	std::lock_guard lock(m_mutex);
	m_errorMessage = std::move(message);
}
