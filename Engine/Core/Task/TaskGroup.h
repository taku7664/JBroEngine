#pragma once

#include "Core/Task/Task.h"
#include "Core/Task/TaskTypes.h"
#include "Utillity/Pointer/SafePtr.h"

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
	// 그룹의 모든 태스크가 끝날 때까지 **메인 스레드에서** 대기한다. 워커가 raw 포인터로
	// 참조하는 소유자를 파괴하기 직전에 부른다(프로젝트 닫기·종료 등). 반환 후에는 이 그룹의
	// 어떤 태스크 본문도 실행 중이지 않다. 대기 중 메인 콜백을 계속 펌프하므로 UI 는 멈춘다.
	void WaitUntilCompleted();

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
