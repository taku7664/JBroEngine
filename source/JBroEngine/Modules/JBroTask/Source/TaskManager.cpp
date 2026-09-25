#include <JBro/Task/TaskManager.h>

#include <chrono>
#include <cwchar>
#include <exception>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace JBro
{
    namespace
    {
        // 디버거의 스레드 목록에서 워커를 알아보게 한다. 이름을 붙이지 못해도 워커는 그대로 돈다.
        void NameWorkerThread(std::uint32_t index)
        {
#if defined(_WIN32)
            wchar_t name[32];
            std::swprintf(name, 32, L"JBro Task Worker %u", index + 1);
            (void)SetThreadDescription(GetCurrentThread(), name);
#else
            (void)index;
#endif
        }

        // 콜백을 부르는 동안 표시를 세운다. 콜백이 던져도 표시가 남지 않는다.
        class CallbackScope
        {
        public:
            explicit CallbackScope(bool& flag)
                : m_flag(flag)
            {
                m_flag = true;
            }
            ~CallbackScope()
            {
                m_flag = false;
            }
            CallbackScope(const CallbackScope&) = delete;
            CallbackScope& operator=(const CallbackScope&) = delete;

        private:
            bool& m_flag;
        };
    }

    TaskManager::TaskManager() = default;

    TaskManager::~TaskManager()
    {
        Shutdown();
    }

    bool TaskManager::Initialize(const TaskManagerDesc& desc)
    {
        if (m_initialized)
        {
            return false;
        }
        m_desc = desc;
#if defined(__EMSCRIPTEN__)
        // 웹에는 스레드가 없다. 기존 엔진도 여기서 워커를 껐다.
        m_useWorkers = false;
#else
        m_useWorkers = desc.useWorkers;
#endif
        m_stopRequested = false;
        m_initialized = true;
        if (m_useWorkers)
        {
            std::uint32_t count = desc.workerCount;
            if (count == 0)
            {
                const unsigned int cores = std::thread::hardware_concurrency();
                count = cores > 1 ? cores - 1 : 1;
            }
            m_workers.Reserve(count);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                m_workers.Emplace([this, i] { WorkerLoop(i); });
            }
            // 워커가 모두 대기에 들어간 뒤에 돌아간다. 스레드가 시작하며 CRT 가 하는 할당이 첫 프레임에
            // 섞이지 않고, 돌아온 뒤에는 워커 수가 곧 받을 수 있는 수다.
            std::unique_lock lock(m_mutex);
            m_workerStarted.wait(lock, [this, count] { return m_startedWorkers == count; });
        }
        return true;
    }

    void TaskManager::Shutdown()
    {
        if (false == m_initialized || m_inCallback)
        {
            return;
        }
        // 아직 시작하지 않은 태스크는 `Run` 없이 `Canceled` 로 끝나고, 돌고 있는 태스크는 표시를 보고 멈추거나 끝까지 돈다.
        for (OwnerPtr<TaskGroup>& group : m_groups)
        {
            if (false == group->m_finished)
            {
                group->RequestCancel();
            }
        }
        {
            std::lock_guard lock(m_mutex);
            m_stopRequested = true;
        }
        m_wake.notify_all();
        // 워커는 큐가 빌 때까지 꺼내 간 뒤에 멈춘다. 순서 묶음의 다음 태스크도 그 사이에 큐로 들어온다.
        for (std::thread& worker : m_workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        m_workers.Clear();
        m_startedWorkers = 0;
        RunOnMainThread(true);
        DrainFinished();
        FinishEmptyGroups();
        m_groups.Clear();
        m_emptyGroupsPending = 0;
        m_ready.Clear();
        m_readyHead = 0;
        m_finished.Clear();
        m_draining.Clear();
        m_useWorkers = false;
        m_initialized = false;
    }

    bool TaskManager::IsInitialized() const
    {
        return m_initialized;
    }

    bool TaskManager::UsesWorkers() const
    {
        return m_useWorkers;
    }

    std::uint32_t TaskManager::GetWorkerCount() const
    {
        return static_cast<std::uint32_t>(m_workers.Size());
    }

    TaskGroupId TaskManager::Submit(OwnerPtr<TaskGroup> group)
    {
        if (false == m_initialized || group.Get() == nullptr || group->m_submitted)
        {
            return InvalidTaskGroupId;
        }
        TaskGroup& submitted = *group;
        submitted.m_submitted = true;
        submitted.m_id = m_nextGroupId++;
        // 워커가 볼 연결을 큐에 넣기 전에 적는다. 큐의 잠금이 이 쓰기를 워커에게 넘긴다.
        const std::uint32_t count = submitted.GetTaskCount();
        const bool sequential = submitted.m_order == TaskGroupOrder::Sequential;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            Task& task = *submitted.m_tasks[i];
            task.m_group = &submitted;
            task.m_next = sequential && i + 1 < count ? submitted.m_tasks[i + 1].Get() : nullptr;
        }
        const TaskGroupId id = submitted.m_id;
        m_groups.Add(std::move(group));
        if (count == 0)
        {
            ++m_emptyGroupsPending;
            return id;
        }
        if (sequential)
        {
            Enqueue(*submitted.m_tasks[0]);
            return id;
        }
        {
            std::lock_guard lock(m_mutex);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                m_ready.Add(submitted.m_tasks[i].Get());
            }
        }
        if (m_useWorkers)
        {
            m_wake.notify_all();
        }
        return id;
    }

    void TaskManager::Update()
    {
        if (false == m_initialized || m_inCallback)
        {
            return;
        }
        if (false == m_useWorkers)
        {
            RunOnMainThread(false);
        }
        DrainFinished();
        FinishEmptyGroups();
        TrimFinishedGroups();
    }

    bool TaskManager::Wait(TaskGroupId id)
    {
        if (false == m_initialized || m_inCallback)
        {
            return false;
        }
        for (;;)
        {
            const TaskGroup* group = FindGroup(id);
            if (group == nullptr || group->m_finished)
            {
                return true;
            }
            if (m_useWorkers)
            {
                // 끝난 태스크가 오면 곧바로 깨고, 오지 않아도 가끔 깨어 살핀다.
                std::unique_lock lock(m_mutex);
                m_taskFinished.wait_for(lock, std::chrono::milliseconds(1), [this] {
                    return false == m_finished.IsEmpty();
                });
            }
            Update();
        }
    }

    TaskGroup* TaskManager::FindGroup(TaskGroupId id)
    {
        for (OwnerPtr<TaskGroup>& group : m_groups)
        {
            if (group->m_id == id)
            {
                return group.Get();
            }
        }
        return nullptr;
    }

    const TaskGroup* TaskManager::FindGroup(TaskGroupId id) const
    {
        for (const OwnerPtr<TaskGroup>& group : m_groups)
        {
            if (group->m_id == id)
            {
                return group.Get();
            }
        }
        return nullptr;
    }

    std::uint32_t TaskManager::GetGroupCount() const
    {
        return static_cast<std::uint32_t>(m_groups.Size());
    }

    const TaskGroup& TaskManager::GetGroupAt(std::uint32_t index) const
    {
        return *m_groups[index];
    }

    void TaskManager::WorkerLoop(std::uint32_t index)
    {
        NameWorkerThread(index);
        {
            std::lock_guard lock(m_mutex);
            ++m_startedWorkers;
        }
        m_workerStarted.notify_one();
        for (;;)
        {
            Task* task = nullptr;
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [this] {
                    return m_stopRequested || m_readyHead < m_ready.Size();
                });
                task = PopReadyLocked();
            }
            if (task == nullptr)
            {
                return;
            }
            RunTask(*task);
        }
    }

    void TaskManager::RunTask(Task& task)
    {
        // 여기는 워커다. 태스크와 묶음은 raw 포인터로만 만진다 - 둘의 수명은 메인의 `m_groups` 가 쥐고 있고,
        // 끝난 태스크의 콜백이 불리기 전에는 지워지지 않는다.
        TaskState finalState = TaskState::Canceled;
        if (false == task.IsCancelRequested())
        {
            task.m_state.store(TaskState::Running, std::memory_order_release);
            bool threw = false;
            String message;
            try
            {
                task.Run();
            }
            catch (const std::exception& exception)
            {
                threw = true;
                message = exception.what();
            }
            catch (...)
            {
                threw = true;
                message = "unknown exception";
            }
            const std::uint32_t num = task.m_numSubTasks;
            if (threw)
            {
                // 던진 자리는 아직 알리지 않은 첫 하위 작업으로 적는다. 다 알린 뒤에 던졌으면 수는 늘리지 않고 사유만 남긴다.
                const std::uint32_t subTask = task.m_reportedSubTasks.load(std::memory_order_relaxed);
                if (false == task.FailSubTask(subTask, message.c_str()))
                {
                    TaskFailure& failure = task.m_failures.Emplace();
                    failure.subTask = subTask;
                    failure.reason = std::move(message);
                }
                finalState = TaskState::Failed;
            }
            else
            {
                const std::uint32_t reported = task.m_reportedSubTasks.load(std::memory_order_relaxed);
                if (task.IsCancelRequested() && reported < num)
                {
                    finalState = TaskState::Canceled;
                }
                else
                {
                    if (reported < num)
                    {
                        task.m_succeededSubTasks.fetch_add(num - reported, std::memory_order_release);
                        task.m_reportedSubTasks.store(num, std::memory_order_relaxed);
                    }
                    finalState = task.GetFailedSubTasks() > 0 ? TaskState::Failed : TaskState::Completed;
                }
            }
        }
        task.m_state.store(finalState, std::memory_order_release);
        Task* next = task.m_next;
        {
            std::lock_guard lock(m_mutex);
            m_finished.Add(&task);
            if (next != nullptr)
            {
                m_ready.Add(next);
            }
        }
        m_taskFinished.notify_all();
        if (next != nullptr && m_useWorkers)
        {
            m_wake.notify_one();
        }
    }

    void TaskManager::Enqueue(Task& task)
    {
        {
            std::lock_guard lock(m_mutex);
            m_ready.Add(&task);
        }
        if (m_useWorkers)
        {
            m_wake.notify_one();
        }
    }

    Task* TaskManager::PopReadyLocked()
    {
        if (m_readyHead >= m_ready.Size())
        {
            return nullptr;
        }
        Task* task = m_ready[m_readyHead];
        ++m_readyHead;
        if (m_readyHead == m_ready.Size())
        {
            m_ready.Clear();
            m_readyHead = 0;
        }
        return task;
    }

    void TaskManager::RunOnMainThread(bool unlimited)
    {
        using Clock = std::chrono::steady_clock;
        const Clock::time_point start = Clock::now();
        for (;;)
        {
            Task* task = nullptr;
            {
                std::lock_guard lock(m_mutex);
                task = PopReadyLocked();
            }
            if (task == nullptr)
            {
                return;
            }
            RunTask(*task);
            const std::chrono::duration<float, std::milli> elapsed = Clock::now() - start;
            if (false == unlimited && elapsed.count() >= m_desc.mainThreadBudgetMs)
            {
                return;
            }
        }
    }

    void TaskManager::DrainFinished()
    {
        {
            std::lock_guard lock(m_mutex);
            m_draining.Swap(m_finished);
        }
        if (m_draining.IsEmpty())
        {
            return;
        }
        const CallbackScope scope(m_inCallback);
        for (Task* task : m_draining)
        {
            task->OnFinished(task->GetResult());
            TaskGroup& group = *task->m_group;
            ++group.m_finishedTasks;
            if (group.m_finishedTasks == group.GetTaskCount())
            {
                FinishGroup(group);
            }
        }
        m_draining.Clear();
    }

    void TaskManager::FinishEmptyGroups()
    {
        if (m_emptyGroupsPending == 0)
        {
            return;
        }
        const CallbackScope scope(m_inCallback);
        // 콜백이 새 묶음을 제출하면 배열이 자란다. 그래서 번호로 돌고 매번 다시 꺼낸다.
        for (std::uint32_t i = 0; i < m_groups.Size(); ++i)
        {
            TaskGroup& group = *m_groups[i];
            if (false == group.m_finished && group.m_tasks.IsEmpty())
            {
                --m_emptyGroupsPending;
                FinishGroup(group);
            }
        }
    }

    void TaskManager::FinishGroup(TaskGroup& group)
    {
        group.m_finished = true;
        group.OnFinished();
    }

    void TaskManager::TrimFinishedGroups()
    {
        std::uint32_t finished = 0;
        for (const OwnerPtr<TaskGroup>& group : m_groups)
        {
            if (group->m_finished)
            {
                ++finished;
            }
        }
        for (std::uint32_t i = 0; i < m_groups.Size() && finished > m_desc.keptFinishedGroups;)
        {
            if (m_groups[i]->m_finished)
            {
                m_groups.RemoveAt(i);
                --finished;
                continue;
            }
            ++i;
        }
    }
}
