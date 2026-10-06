#include <JBro/Task/TaskManager.h>

#include <chrono>
#include <cwchar>
#include <exception>
#include <utility>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/Float.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace JBro
{
    namespace
    {
        // 디버거의 스레드 목록에서 워커를 알아보게 한다. 이름을 붙이지 못해도 워커는 그대로 돈다.
        void NameWorkerThread(UInt32 index)
        {
#if defined(_WIN32)
            wchar_t name[32];
            std::swprintf(name, 32, L"JBro Task Worker %u", (index + 1).Get());
            (void)SetThreadDescription(GetCurrentThread(), name);
#else
            (void)index;
#endif
        }

        // 콜백을 부르는 동안 깊이를 센다. 콜백 안에서 `Update` 가 다시 들어오면 둘이 된다. 콜백이 던져도 수가 남지 않는다.
        class CallbackScope
        {
        public:
            explicit CallbackScope(UInt32& depth)
                : m_depth(depth)
            {
                ++m_depth;
            }
            ~CallbackScope()
            {
                --m_depth;
            }
            CallbackScope(const CallbackScope&) = delete;
            CallbackScope& operator=(const CallbackScope&) = delete;

        private:
            UInt32& m_depth;
        };
    }

    TaskManager::TaskManager() = default;

    TaskManager::~TaskManager()
    {
        Shutdown();
    }

    Bool TaskManager::Initialize(const TaskManagerDesc& desc)
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
            UInt32 count = desc.workerCount;
            if (count == 0)
            {
                const unsigned int cores = std::thread::hardware_concurrency();
                count = cores > 1 ? cores - 1 : 1;
            }
            m_workers.Reserve(count);
            for (UInt32 i = 0; i < count; ++i)
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
        if (false == m_initialized || m_callbackDepth > 0)
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
        FinishReadyGroups();
        m_groups.Clear();
        m_groupsMayBeReady = false;
        m_ready.Clear();
        m_readyHead = 0;
        m_finished.Clear();
        m_finishedHead = 0;
        m_useWorkers = false;
        m_initialized = false;
    }

    Bool TaskManager::IsInitialized() const
    {
        return m_initialized;
    }

    Bool TaskManager::UsesWorkers() const
    {
        return m_useWorkers;
    }

    UInt32 TaskManager::GetWorkerCount() const
    {
        return static_cast<JBro::UInt32>(m_workers.Size());
    }

    TaskGroupId TaskManager::Submit(OwnerPtr<TaskGroup> group)
    {
        // 제출한 묶음을 다시 제출하는 길은 없다 - 소유가 여기로 넘어와 호출자에게는 빈 포인터만 남는다.
        if (false == m_initialized || group.Get() == nullptr)
        {
            return InvalidTaskGroupId;
        }
        TaskGroup& submitted = *group;
        submitted.m_submitted = true;
        submitted.m_id = m_nextGroupId++;
        // 워커가 볼 연결을 큐에 넣기 전에 적는다. 큐의 잠금이 이 쓰기를 워커에게 넘긴다.
        const UInt32 count = submitted.GetTaskCount();
        const Bool sequential = submitted.m_order == TaskGroupOrder::Sequential;
        for (UInt32 i = 0; i < count; ++i)
        {
            Task& task = *submitted.m_tasks[i];
            task.m_group = &submitted;
            task.m_next = sequential && i + 1 < count ? submitted.m_tasks[i + 1].Get() : nullptr;
        }
        const TaskGroupId id = submitted.m_id;
        m_groups.Add(std::move(group));
        if (count == 0)
        {
            m_groupsMayBeReady = true;
            return id;
        }
        if (sequential)
        {
            Enqueue(*submitted.m_tasks[0]);
            return id;
        }
        {
            std::lock_guard lock(m_mutex);
            for (UInt32 i = 0; i < count; ++i)
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
        if (false == m_initialized)
        {
            return;
        }
        if (false == m_useWorkers)
        {
            RunOnMainThread(false);
        }
        DrainFinished();
        FinishReadyGroups();
        // 콜백 안에서 들어온 `Update` 는 묶음을 지우지 않는다 - 바깥에서 콜백이 불리고 있는 태스크의 묶음일 수 있다.
        if (m_callbackDepth == 0)
        {
            TrimFinishedGroups();
        }
    }

    Bool TaskManager::Wait(TaskGroupId id)
    {
        if (false == m_initialized)
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
                    return m_finishedHead < m_finished.Size();
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

    UInt32 TaskManager::GetGroupCount() const
    {
        return static_cast<JBro::UInt32>(m_groups.Size());
    }

    const TaskGroup& TaskManager::GetGroupAt(UInt32 index) const
    {
        return *m_groups[index];
    }

    void TaskManager::WorkerLoop(UInt32 index)
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
            Bool threw = false;
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
            const UInt32 num = task.m_numSubTasks;
            if (threw)
            {
                // 던진 자리는 아직 알리지 않은 첫 하위 작업으로 적는다. 다 알린 뒤에 던졌으면 수는 늘리지 않고 사유만 남긴다.
                const UInt32 subTask = task.m_reportedSubTasks.load(std::memory_order_relaxed);
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
                const UInt32 reported = task.m_reportedSubTasks.load(std::memory_order_relaxed);
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

    void TaskManager::RunOnMainThread(Bool unlimited)
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
        // 하나씩 꺼내 부른다. 콜백 안에서 `Update` 가 다시 들어와도 남은 것을 이어서 꺼낼 뿐이고, 콜백이 던져도
        // 이미 꺼낸 것은 다시 불리지 않고 남은 것은 다음 `Update` 에서 불린다.
        for (;;)
        {
            Task* task = nullptr;
            {
                std::lock_guard lock(m_mutex);
                if (m_finishedHead >= m_finished.Size())
                {
                    m_finished.Clear();
                    m_finishedHead = 0;
                    return;
                }
                task = m_finished[m_finishedHead];
                ++m_finishedHead;
            }
            // 묶음의 셈은 콜백 전에 한다. 콜백이 던져도 묶음은 끝날 수 있어야 한다.
            TaskGroup& group = *task->m_group;
            ++group.m_finishedTasks;
            if (group.m_finishedTasks == group.GetTaskCount())
            {
                m_groupsMayBeReady = true;
            }
            const CallbackScope scope(m_callbackDepth);
            task->OnFinished(task->GetResult());
        }
    }

    void TaskManager::FinishReadyGroups()
    {
        if (false == m_groupsMayBeReady)
        {
            return;
        }
        m_groupsMayBeReady = false;
        // 콜백이 새 묶음을 제출하면 배열이 자란다. 그래서 번호로 돌고 매번 다시 꺼낸다.
        for (UInt32 i = 0; i < m_groups.Size(); ++i)
        {
            TaskGroup& group = *m_groups[i];
            if (group.m_finished || group.m_finishedTasks != group.GetTaskCount())
            {
                continue;
            }
            // 부르기 전에 끝난 것으로 적는다. 콜백이 던져도 다시 불리지 않고, 남은 묶음은 다음 `Update` 가 본다.
            group.m_finished = true;
            try
            {
                const CallbackScope scope(m_callbackDepth);
                group.OnFinished();
            }
            catch (...)
            {
                m_groupsMayBeReady = true;
                throw;
            }
        }
    }

    void TaskManager::TrimFinishedGroups()
    {
        UInt32 finished = 0;
        for (const OwnerPtr<TaskGroup>& group : m_groups)
        {
            if (group->m_finished)
            {
                ++finished;
            }
        }
        for (UInt32 i = 0; i < m_groups.Size() && finished > m_desc.keptFinishedGroups;)
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
