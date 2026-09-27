#pragma once

#include <JBro/Task/Task.h>
#include <JBro/Task/TaskGroup.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

// 엔진과 에디터가 함께 쓰는 태스크 관리자다(D-209). 기존 엔진 `CTaskManager` 자리다.
//
// Tier E 커널이다 - 캔버스도 스크립트도 모른다. 워커가 `SafePtr` 를 못 만지는 계약을 스크립트 작성자에게 넘기지
// 않기 위해 스크립트에는 보이지 않는다. 쓰는 곳은 에디터 로딩(D-208)·게임의 비동기 로드·미리 읽기 워커다.
// 오디오 스트리머(실시간 마감)와 파일 감시(OS 대기에서 막힌다)는 제 스레드를 그대로 든다.
//
// 공개 함수는 모두 **메인 스레드 전용**이다. 워커는 관리자 안에서 잠금 아래의 큐만 만진다.
namespace JBro
{
    struct TaskManagerDesc
    {
        // 워커 수다. 0 이면 코어 수 - 1(최소 1)로 정한다.
        std::uint32_t workerCount = 0;
        // 거짓이면(웹은 늘 거짓) 워커를 세우지 않고 `Update` 가 메인 스레드에서 태스크를 차례로 돌린다.
        bool useWorkers = true;
        // 워커가 없을 때 `Update` 한 번이 태스크에 쓰는 시간이다. 이 시간을 넘기면 멈추되, 적어도 하나는 돌린다.
        float mainThreadBudgetMs = 4.0f;
        // 끝난 묶음을 현황표에 남겨 두는 개수다. 넘치면 오래된 것부터 지운다.
        std::uint32_t keptFinishedGroups = 16;
    };

    class TaskManager
    {
    public:
        TaskManager();
        // `Shutdown` 을 부른다.
        ~TaskManager();
        TaskManager(const TaskManager&) = delete;
        TaskManager& operator=(const TaskManager&) = delete;

        bool Initialize(const TaskManagerDesc& desc = {});
        // 남은 태스크에 모두 취소 표시를 하고, 돌고 있는 태스크가 끝나기를 기다린 뒤 남은 콜백을 모두 부르고
        // 묶음을 지운다. 콜백 안에서 부르면 아무 일도 하지 않는다 - 부르고 있는 태스크가 그 안에서 지워진다.
        void Shutdown();
        bool IsInitialized() const;
        bool UsesWorkers() const;
        std::uint32_t GetWorkerCount() const;

        // 묶음을 넘겨받아 돌리기 시작한다. 초기화 전이거나 빈 포인터면 `InvalidTaskGroupId` 다.
        // 콜백은 이 호출 안에서 불리지 않는다 - 빈 묶음도 다음 `Update` 에서 끝난다.
        TaskGroupId Submit(OwnerPtr<TaskGroup> group);

        // 프레임마다 한 번 부른다. 워커가 없으면 예산만큼 태스크를 돌리고, 끝난 태스크와 묶음의 `OnFinished` 를
        // 부른다. 콜백 안에서 불러도 된다 - 남은 콜백을 이어서 부르고, 끝난 묶음은 지우지 않는다.
        // 콜백이 던지면 그 예외가 여기서 나온다. 이미 부른 콜백은 다시 불리지 않고 남은 것은 다음 `Update` 에서 불린다.
        void Update();

        // 그 묶음이 끝날 때까지(콜백까지) 메인 스레드를 세운다. 소유자를 부수기 직전에 부른다.
        // 없는 묶음이면 이미 끝나 지워진 것이라 참이다. 콜백 안에서 불러도 된다. 초기화 전이면 거짓이다.
        bool Wait(TaskGroupId id);

        // 현황표용이다. 돌려받은 포인터는 다음 `Update` 전까지만 쓴다 - 끝난 묶음은 거기서 지워질 수 있다.
        TaskGroup* FindGroup(TaskGroupId id);
        const TaskGroup* FindGroup(TaskGroupId id) const;
        std::uint32_t GetGroupCount() const;
        const TaskGroup& GetGroupAt(std::uint32_t index) const;

    private:
        void WorkerLoop(std::uint32_t index);
        // 워커(또는 워커가 없을 때 메인)가 태스크 하나를 돌리고 끝난 큐에 넣는다.
        void RunTask(Task& task);
        void Enqueue(Task& task);
        // 잠금 아래에서 부른다.
        Task* PopReadyLocked();
        void RunOnMainThread(bool unlimited);
        void DrainFinished();
        // 태스크가 모두 끝난(빈 묶음 포함) 묶음의 `OnFinished` 를 부른다.
        void FinishReadyGroups();
        void TrimFinishedGroups();

        TaskManagerDesc m_desc;
        bool m_initialized = false;
        bool m_useWorkers = false;
        // 콜백을 부르는 깊이다. 0 이 아니면 `Shutdown` 은 거절하고 `Update` 는 묶음을 지우지 않는다.
        std::uint32_t m_callbackDepth = 0;
        TaskGroupId m_nextGroupId = 1;
        // 메인 스레드만 만진다.
        Array<OwnerPtr<TaskGroup>> m_groups;
        // 끝낼 묶음이 있을 수 있다. 거짓이면 `Update` 가 묶음을 훑지 않는다.
        bool m_groupsMayBeReady = false;
        // 아래는 `m_mutex` 아래에서만 만진다. 큐는 앞에서 꺼내므로 머리 번호를 따로 두고 비면 되감는다 -
        // 프레임마다 힙 할당을 하지 않는다.
        std::mutex m_mutex;
        std::condition_variable m_wake;
        std::condition_variable m_taskFinished;
        std::condition_variable m_workerStarted;
        std::uint32_t m_startedWorkers = 0;
        Array<Task*> m_ready;
        std::uint32_t m_readyHead = 0;
        // 끝난 태스크다. `m_ready` 처럼 앞에서 꺼내고 비면 되감는다.
        Array<Task*> m_finished;
        std::uint32_t m_finishedHead = 0;
        bool m_stopRequested = false;
        Array<std::thread> m_workers;
    };
}
