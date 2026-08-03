#include "pch.h"
#include "TaskGroup.h"

#include "Core/Task/Task.h"
#include "Core/Task/TaskManager.h"

#include <chrono>
#include <thread>

CTaskGroup::CTaskGroup(CTaskManager& manager, TaskId id, std::string name)
	: m_manager(&manager)
	, m_id(id)
	, m_name(std::move(name))
{
}

TaskId CTaskGroup::GetId() const
{
	return m_id;
}

const std::string& CTaskGroup::GetName() const
{
	return m_name;
}

SafePtr<CTask> CTaskGroup::CreateTask(const char* name, CTask::TaskFunction function, const char* description)
{
	return m_manager ? m_manager->CreateTask(name, std::move(function), SafeFromThis(), description) : nullptr;
}

std::uint32_t CTaskGroup::GetTotalTaskCount() const
{
	std::lock_guard lock(m_mutex);
	return static_cast<std::uint32_t>(m_tasks.size());
}

std::uint32_t CTaskGroup::GetCompletedTaskCount() const
{
	return m_completedTaskCount.load();
}

float CTaskGroup::GetProgress01() const
{
	const std::uint32_t total = GetTotalTaskCount();
	return 0 == total ? 1.0f : static_cast<float>(GetCompletedTaskCount()) / static_cast<float>(total);
}

bool CTaskGroup::IsCompleted() const
{
	const std::uint32_t total = GetTotalTaskCount();
	return total > 0 && GetCompletedTaskCount() >= total;
}

std::vector<TaskProgressInfo> CTaskGroup::GetTaskProgressSnapshot() const
{
	std::vector<TaskProgressInfo> snapshot;
	std::lock_guard lock(m_mutex);
	snapshot.reserve(m_tasks.size());
	for (const SafePtr<CTask>& task : m_tasks)
	{
		if (false == task.IsValid())
		{
			continue;
		}
		TaskProgressInfo info;
		info.Name        = task->GetName();
		info.Description  = task->GetDescription();
		info.Completed    = task->IsFinished();
		snapshot.push_back(std::move(info));
	}
	return snapshot;
}

void CTaskGroup::WaitUntilCompleted()
{
	if (nullptr == m_manager)
	{
		return;
	}

	// 판정에 IsCompleted() 를 쓰지 않는다 — 그 함수는 태스크가 0 개면 false 를 돌려주므로
	// (진행률 UI 용 의미) 빈 그룹에서 무한 대기가 된다. 여기서는 "완료 수 < 전체 수" 를 본다.
	//
	// 완료 카운트는 워커가 아니라 **메인 스레드 콜백 드레인**(RunTask 가 큐잉한 콜백 →
	// NotifyTaskFinished)에서만 올라간다. 그래서 그냥 잠들면 영영 끝나지 않는다 —
	// 대기 루프가 직접 드레인을 펌프하는 것이 곧 진행이다.
	while (GetCompletedTaskCount() < GetTotalTaskCount())
	{
		m_manager->DrainMainThreadCallbacks();
		if (GetCompletedTaskCount() >= GetTotalTaskCount())
		{
			break;
		}
		// 워커가 실제로 일하는 동안은 이 스레드가 할 일이 없다. yield 스핀은 종료가 긴
		// 태스크(스크립트 빌드 수 초)에서 코어 하나를 통째로 태우므로 짧게 잠든다.
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	// 마지막 태스크의 EndCallback / AllCompletedCallback 이 아직 큐에 남아 있을 수 있다.
	m_manager->DrainMainThreadCallbacks();
}

void CTaskGroup::AddTask(SafePtr<CTask> task)
{
	if (task)
	{
		std::lock_guard lock(m_mutex);
		m_tasks.push_back(task);
	}
}

void CTaskGroup::NotifyTaskFinished()
{
	const std::uint32_t completed = ++m_completedTaskCount;
	const std::uint32_t total = GetTotalTaskCount();
	if (m_manager && total > 0 && completed >= total && false == m_allCompletedQueued.exchange(true))
	{
		m_manager->PostMainThreadTask([group = SafeFromThis()]()
		{
			if (group && group->AllCompletedCallback)
			{
				group->AllCompletedCallback();
			}
		});
	}
}
