#pragma once

#include <JBro/Task/Task.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>

#include <cstdint>

// 패널·툴·게임이 한 번에 등록하는 태스크 묶음이다(D-208·D-209). 기존 엔진 `CTaskGroup` 자리다.
//
// **묶음은 다 채운 뒤 통째로 제출한다.** 기존 엔진은 큐에 넣는 도중 빠른 태스크가 먼저 끝나면
// `AllCompletedCallback` 이 일찍 불려, `ProjectManager.cpp` 가 총 개수를 미리 세고 자기 카운터로 끝을 판정했다.
// 여기서는 제출하는 순간 태스크 수가 굳고, 관리자는 그 뒤에만 완료를 판정한다.
namespace JBro
{
    using TaskGroupId = std::uint64_t;
    inline constexpr TaskGroupId InvalidTaskGroupId = 0;

    // 묶음 안의 태스크를 어떻게 돌리는가. 태스크 사이의 개별 의존은 두지 않는다 - 묶음을 나누거나
    // `OnFinished` 에서 다음 묶음을 제출해 조정한다.
    enum class TaskGroupOrder : std::uint8_t
    {
        // 워커들이 동시에 꺼내 간다.
        Parallel,
        // 앞 태스크가 끝나야 다음이 시작한다. 앞이 실패하거나 취소되어도 다음은 돈다 - 멈추려면 취소한다.
        Sequential,
    };

    class TaskGroup
    {
    public:
        explicit TaskGroup(String name, TaskGroupOrder order = TaskGroupOrder::Parallel);
        virtual ~TaskGroup();
        TaskGroup(const TaskGroup&) = delete;
        TaskGroup& operator=(const TaskGroup&) = delete;

        // 제출하기 전에만 더할 수 있다. 제출한 뒤이거나 비어 있는 포인터면 거짓이다.
        bool Add(OwnerPtr<Task> task);

        const String& GetName() const;
        TaskGroupOrder GetOrder() const;
        // 제출하기 전에는 `InvalidTaskGroupId` 다.
        TaskGroupId GetId() const;
        std::uint32_t GetTaskCount() const;
        const Task& GetTaskAt(std::uint32_t index) const;
        bool IsSubmitted() const;
        // 모든 태스크의 `OnFinished` 와 이 묶음의 `OnFinished` 가 불린 뒤에 참이다.
        bool IsFinished() const;
        // 현황표의 한 줄이다. 끝나기 전에는 하나라도 시작했으면 `Running`, 아니면 `Pending` 이다. 끝난 뒤에는
        // 하나라도 실패했으면 `Failed`, 아니면 하나라도 취소되었으면 `Canceled`, 아니면 `Completed` 다.
        TaskState GetState() const;
        // 묶음의 모든 태스크에 취소 표시를 한다. 메인 스레드에서 부른다.
        void RequestCancel();

    protected:
        // 메인 스레드에서 한 번, 모든 태스크의 `OnFinished` 뒤에 불린다.
        virtual void OnFinished();

    private:
        friend class TaskManager;

        String m_name;
        TaskGroupOrder m_order = TaskGroupOrder::Parallel;
        TaskGroupId m_id = InvalidTaskGroupId;
        // 메인 스레드만 만진다. 워커는 제출 때 떠 둔 raw 포인터(`Task::m_next`, 관리자의 큐)만 본다.
        Array<OwnerPtr<Task>> m_tasks;
        std::uint32_t m_finishedTasks = 0;
        bool m_submitted = false;
        bool m_finished = false;
    };
}
