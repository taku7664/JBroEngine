#include <JBro/Task/TaskManager.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Host/EngineInstance.h>
#include <JBro/Platform/WindowsPlatform.h>

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

// 태스크 관리자 JBroTask 의 테스트(D-209). todo.md "그 밖의 공용" 의 완료 조건을 하나씩 잰다:
// 병렬 묶음이 여러 워커에서 실제로 동시에 도는가, 순서 묶음은 앞이 끝나기 전에 다음이 시작하지 않는가,
// 진행률이 0/N 에서 N/N 까지 오르고 실패 수가 따로 세는가, 콜백이 메인 스레드에서만 불리는가, 워커 0 개에서도
// 같은가, 종료가 도는 태스크를 기다리고 남은 콜백을 비우는가, 취소 표시를 본 태스크가 Canceled 로 끝나는가.
namespace
{
    using JBro::OwnerPtr;
    using JBro::Task;
    using JBro::TaskGroup;
    using JBro::TaskGroupId;
    using JBro::TaskGroupOrder;
    using JBro::TaskManager;
    using JBro::TaskManagerDesc;
    using JBro::TaskResult;
    using JBro::TaskState;
    using Clock = std::chrono::steady_clock;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    // 워커에서 기다리는 자리가 영원히 멈추지 않게 한다. 넘기면 거짓이고, 테스트가 그것을 실패로 본다.
    template<typename Predicate>
    bool SpinUntil(Predicate&& predicate, int milliseconds = 10000)
    {
        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(milliseconds);
        while (false == predicate())
        {
            if (Clock::now() > deadline)
            {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    }

    // 태스크와 묶음이 적는 자리다. 관리자보다 오래 산다 - 태스크는 콜백 뒤에 지워질 수 있다.
    struct Journal
    {
        std::thread::id mainThread;
        std::atomic<int> runs = 0;
        std::atomic<int> running = 0;
        std::atomic<int> maxRunning = 0;
        std::atomic<int> startOrder[16] = {};
        std::atomic<int> nextStart = 0;
        std::atomic<bool> ranOnMain = false;
        std::atomic<bool> ranOnWorker = false;
        std::atomic<bool> timedOut = false;
        int finished = 0;
        int finishedOffMain = 0;
        int groupFinished = 0;
        int groupFinishedOffMain = 0;
        // 묶음의 콜백이 불렸을 때 이미 불린 태스크 콜백 수다.
        int tasksFinishedBeforeGroup = -1;
        TaskState lastState = TaskState::Pending;
        std::uint32_t lastSucceeded = 0;
        std::uint32_t lastFailed = 0;
        char lastReason[128] = {};
        std::uint32_t lastReasonSubTask = 0;
        std::uint32_t lastFailureCount = 0;
    };

    void EnterRun(Journal& journal, int index)
    {
        ++journal.runs;
        const int now = ++journal.running;
        int seen = journal.maxRunning.load();
        while (now > seen && false == journal.maxRunning.compare_exchange_weak(seen, now))
        {
        }
        if (index >= 0 && index < 16)
        {
            journal.startOrder[journal.nextStart++ % 16] = index;
        }
        if (std::this_thread::get_id() == journal.mainThread)
        {
            journal.ranOnMain = true;
        }
        else
        {
            journal.ranOnWorker = true;
        }
    }

    void LeaveRun(Journal& journal)
    {
        --journal.running;
    }

    // 콜백의 공통 기록이다.
    void RecordFinished(Journal& journal, const TaskResult& result)
    {
        ++journal.finished;
        if (std::this_thread::get_id() != journal.mainThread)
        {
            ++journal.finishedOffMain;
        }
        journal.lastState = result.state;
        journal.lastSucceeded = result.succeededSubTasks;
        journal.lastFailed = result.failedSubTasks;
        journal.lastFailureCount = static_cast<std::uint32_t>(result.failures.Size());
        if (false == result.failures.IsEmpty())
        {
            strncpy_s(journal.lastReason, result.failures[0].reason.c_str(), _TRUNCATE);
            journal.lastReasonSubTask = result.failures[0].subTask;
        }
    }

    class RecordingGroup final : public TaskGroup
    {
    public:
        RecordingGroup(Journal& journal, const char* name, TaskGroupOrder order)
            : TaskGroup(name, order)
            , m_journal(journal)
        {
        }

    protected:
        void OnFinished() override
        {
            ++m_journal.groupFinished;
            if (std::this_thread::get_id() != m_journal.mainThread)
            {
                ++m_journal.groupFinishedOffMain;
            }
            m_journal.tasksFinishedBeforeGroup = m_journal.finished;
        }

    private:
        Journal& m_journal;
    };

    // 모두가 한자리에 모일 때까지 기다린다. 동시에 돌지 않으면 모이지 못하고 시간이 넘는다.
    class BarrierTask final : public Task
    {
    public:
        BarrierTask(Journal& journal, std::atomic<int>& arrived, int expected)
            : Task("barrier")
            , m_journal(journal)
            , m_arrived(arrived)
            , m_expected(expected)
        {
        }

    protected:
        void Run() override
        {
            EnterRun(m_journal, -1);
            ++m_arrived;
            if (false == SpinUntil([this] { return m_arrived.load() >= m_expected; }, 5000))
            {
                m_journal.timedOut = true;
            }
            LeaveRun(m_journal);
        }

        void OnFinished(const TaskResult& result) override
        {
            RecordFinished(m_journal, result);
        }

    private:
        Journal& m_journal;
        std::atomic<int>& m_arrived;
        int m_expected;
    };

    // 잠깐 도는 태스크다. 순서 묶음에서 겹치면 maxRunning 이 1 을 넘는다.
    class SleepTask final : public Task
    {
    public:
        SleepTask(Journal& journal, int index, int milliseconds)
            : Task("sleep")
            , m_journal(journal)
            , m_index(index)
            , m_milliseconds(milliseconds)
        {
        }

    protected:
        void Run() override
        {
            EnterRun(m_journal, m_index);
            if (m_milliseconds > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(m_milliseconds));
            }
            LeaveRun(m_journal);
        }

        void OnFinished(const TaskResult& result) override
        {
            RecordFinished(m_journal, result);
        }

    private:
        Journal& m_journal;
        int m_index;
        int m_milliseconds;
    };

    // 하위 작업 N 개를 하나씩 끝낸다. gate 가 있으면 메인이 gate 를 올릴 때마다 하나씩만 나아간다.
    // failAt 에 든 번호는 실패로 알린다.
    class SubTaskLoader final : public Task
    {
    public:
        SubTaskLoader(Journal& journal, std::uint32_t count, std::atomic<std::uint32_t>* gate,
            std::uint32_t failA, std::uint32_t failB)
            : Task("loader", count)
            , m_journal(journal)
            , m_gate(gate)
            , m_failA(failA)
            , m_failB(failB)
        {
        }

    protected:
        void Run() override
        {
            EnterRun(m_journal, -1);
            for (std::uint32_t i = 0; i < GetNumSubTasks(); ++i)
            {
                if (m_gate != nullptr && false == SpinUntil([this, i] { return m_gate->load() > i; }))
                {
                    m_journal.timedOut = true;
                    break;
                }
                if (i == m_failA || i == m_failB)
                {
                    char reason[64];
                    sprintf_s(reason, "cannot read sheet_%u.png", i);
                    FailSubTask(i, reason);
                }
                else
                {
                    SucceedSubTask();
                }
            }
            // 넘치는 알림은 세지 않는다.
            m_overReportRejected = false == SucceedSubTask() && false == FailSubTask(99, "over");
            LeaveRun(m_journal);
        }

        void OnFinished(const TaskResult& result) override
        {
            RecordFinished(m_journal, result);
        }

    public:
        bool m_overReportRejected = false;

    private:
        Journal& m_journal;
        std::atomic<std::uint32_t>* m_gate;
        std::uint32_t m_failA;
        std::uint32_t m_failB;
    };

    // 취소 표시를 볼 때까지 하위 작업을 하나씩 끝낸다. 한 번은 반드시 돈다.
    class CancelAwareTask final : public Task
    {
    public:
        CancelAwareTask(Journal& journal, std::atomic<bool>& started)
            : Task("cancel-aware", 1000)
            , m_journal(journal)
            , m_started(started)
        {
        }

    protected:
        void Run() override
        {
            EnterRun(m_journal, -1);
            m_started = true;
            for (std::uint32_t i = 0; i < GetNumSubTasks(); ++i)
            {
                if (IsCancelRequested())
                {
                    break;
                }
                SucceedSubTask();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            LeaveRun(m_journal);
        }

        void OnFinished(const TaskResult& result) override
        {
            RecordFinished(m_journal, result);
        }

    private:
        Journal& m_journal;
        std::atomic<bool>& m_started;
    };

    class ThrowingTask final : public Task
    {
    public:
        explicit ThrowingTask(Journal& journal)
            : Task("throws", 3)
            , m_journal(journal)
        {
        }

    protected:
        void Run() override
        {
            SucceedSubTask();
            throw std::runtime_error("decoder exploded");
        }

        void OnFinished(const TaskResult& result) override
        {
            RecordFinished(m_journal, result);
        }

    private:
        Journal& m_journal;
    };

    // 워커 스레드의 이름을 읽어 둔다.
    class NameProbeTask final : public Task
    {
    public:
        explicit NameProbeTask(wchar_t* out)
            : Task("name")
            , m_out(out)
        {
        }

    protected:
        void Run() override
        {
            PWSTR description = nullptr;
            if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &description)) && description != nullptr)
            {
                wcsncpy_s(m_out, 64, description, _TRUNCATE);
                LocalFree(description);
            }
        }

    private:
        wchar_t* m_out;
    };

    TaskManagerDesc Workers(std::uint32_t count)
    {
        TaskManagerDesc desc;
        desc.workerCount = count;
        desc.useWorkers = true;
        return desc;
    }

    TaskManagerDesc MainThreadOnly(float budgetMs = 1000.0f)
    {
        TaskManagerDesc desc;
        desc.useWorkers = false;
        desc.mainThreadBudgetMs = budgetMs;
        return desc;
    }

    // 끝날 때까지 Update 를 돈다. 시간 안에 끝나지 않으면 거짓이다.
    bool PumpUntilFinished(TaskManager& manager, TaskGroupId id)
    {
        return SpinUntil([&] {
            manager.Update();
            const TaskGroup* group = manager.FindGroup(id);
            return group == nullptr || group->IsFinished();
        });
    }

    void TestParallelGroupRunsOnSeveralWorkersAtOnce()
    {
        Journal journal;
        journal.mainThread = std::this_thread::get_id();
        std::atomic<int> arrived = 0;
        TaskManager manager;
        Check(manager.Initialize(Workers(4)), "a manager with four workers must start");
        Check(manager.UsesWorkers() && manager.GetWorkerCount() == 4, "it must hold the four workers it was asked for");

        OwnerPtr<RecordingGroup> group = JBro::MakeOwnerPtr<RecordingGroup>(journal, "parallel", TaskGroupOrder::Parallel);
        for (int i = 0; i < 4; ++i)
        {
            group->Add(JBro::MakeOwnerPtr<BarrierTask>(journal, arrived, 4));
        }
        const TaskGroupId id = manager.Submit(std::move(group));
        Check(id != JBro::InvalidTaskGroupId, "submitting a filled group must give it an id");
        Check(PumpUntilFinished(manager, id), "the parallel group must finish");
        Check(false == journal.timedOut, "four barrier tasks must meet - they can only if they run at the same time");
        Check(journal.maxRunning.load() == 4, "all four must have been running together");
        Check(journal.ranOnWorker && false == journal.ranOnMain, "the bodies must run on workers, not the main thread");
        Check(journal.finished == 4 && journal.finishedOffMain == 0, "every task callback must come on the main thread");
        Check(journal.groupFinished == 1 && journal.groupFinishedOffMain == 0, "the group callback must come once, on the main thread");
        Check(journal.tasksFinishedBeforeGroup == 4, "the group callback must come after all four task callbacks");
        const TaskGroup* finished = manager.FindGroup(id);
        Check(finished != nullptr && finished->GetState() == TaskState::Completed, "the group must read Completed");
        manager.Shutdown();
    }

    void CheckSequentialGroup(TaskManager& manager, Journal& journal)
    {
        OwnerPtr<RecordingGroup> group = JBro::MakeOwnerPtr<RecordingGroup>(journal, "sequential", TaskGroupOrder::Sequential);
        for (int i = 0; i < 6; ++i)
        {
            group->Add(JBro::MakeOwnerPtr<SleepTask>(journal, i, 3));
        }
        const TaskGroupId id = manager.Submit(std::move(group));
        Check(PumpUntilFinished(manager, id), "the sequential group must finish");
        Check(journal.runs.load() == 6, "every task of the sequential group must run");
        Check(journal.maxRunning.load() == 1, "no task of a sequential group may start before the one before it ends");
        for (int i = 0; i < 6; ++i)
        {
            Check(journal.startOrder[i].load() == i, "a sequential group must start its tasks in the order they were added");
        }
        Check(journal.finished == 6 && journal.finishedOffMain == 0, "each task callback must come on the main thread");
        Check(journal.tasksFinishedBeforeGroup == 6, "the group callback must follow every task callback");
    }

    void TestSequentialGroupNeverOverlaps()
    {
        Journal journal;
        journal.mainThread = std::this_thread::get_id();
        TaskManager manager;
        Check(manager.Initialize(Workers(4)), "the manager must start");
        CheckSequentialGroup(manager, journal);
        Check(journal.ranOnWorker && false == journal.ranOnMain, "sequential bodies still run on workers");
        manager.Shutdown();
    }

    void TestProgressCountsUpAndFailuresSeparately()
    {
        Journal journal;
        journal.mainThread = std::this_thread::get_id();
        std::atomic<std::uint32_t> gate = 0;
        TaskManager manager;
        Check(manager.Initialize(Workers(2)), "the manager must start");
        OwnerPtr<TaskGroup> group = JBro::MakeOwnerPtr<TaskGroup>("load sheets");
        group->Add(JBro::MakeOwnerPtr<SubTaskLoader>(journal, 40, &gate, 3, 17));
        const TaskGroupId id = manager.Submit(std::move(group));
        const Task& task = manager.FindGroup(id)->GetTaskAt(0);
        Check(task.GetNumSubTasks() == 40 && task.GetSucceededSubTasks() == 0 && task.GetFailedSubTasks() == 0,
            "a task of forty sub-tasks must start at 0/40");
        std::uint32_t lastDone = 0;
        for (std::uint32_t step = 1; step <= 40; ++step)
        {
            gate = step;
            Check(SpinUntil([&] { return task.GetSucceededSubTasks() + task.GetFailedSubTasks() >= step; }),
                "each opened step must be counted");
            const std::uint32_t done = task.GetSucceededSubTasks() + task.GetFailedSubTasks();
            Check(done == step, "the count must rise one sub-task at a time, never ahead of the work");
            Check(done > lastDone, "the count must only rise");
            lastDone = done;
            if (step == 10)
            {
                Check(task.GetFailedSubTasks() == 1 && task.GetSucceededSubTasks() == 9,
                    "one failure by the tenth step must be counted apart from the nine successes");
                Check(task.GetState() == TaskState::Running, "a task part way through must read Running");
            }
        }
        Check(PumpUntilFinished(manager, id), "the loader must finish");
        Check(false == journal.timedOut, "the loader must never wait past its gate");
        Check(journal.lastState == TaskState::Failed, "a task with failed sub-tasks must end Failed");
        Check(journal.lastSucceeded == 38 && journal.lastFailed == 2, "it must end at 38 succeeded and 2 failed of 40");
        Check(journal.lastFailureCount == 2, "the result must carry one reason per failed sub-task");
        Check(journal.lastReasonSubTask == 3 && std::strcmp(journal.lastReason, "cannot read sheet_3.png") == 0,
            "the first reason must name the sub-task and the file it could not read");
        const auto* loader = static_cast<const SubTaskLoader*>(&manager.FindGroup(id)->GetTaskAt(0));
        Check(loader->m_overReportRejected, "reports past the sub-task count must be refused");
        Check(manager.FindGroup(id)->GetState() == TaskState::Failed, "the group must read Failed");
        manager.Shutdown();
    }

    void TestUnreportedSubTasksCountAsDone()
    {
        Journal journal;
        journal.mainThread = std::this_thread::get_id();
        TaskManager manager;
        Check(manager.Initialize(Workers(1)), "the manager must start");
        OwnerPtr<TaskGroup> group = JBro::MakeOwnerPtr<TaskGroup>("plain");
        group->Add(JBro::MakeOwnerPtr<SleepTask>(journal, 0, 0));
        const TaskGroupId id = manager.Submit(std::move(group));
        Check(manager.Wait(id), "waiting on a live group must succeed");
        Check(journal.finished == 1, "waiting must return only after the task callback");
        Check(journal.lastState == TaskState::Completed && journal.lastSucceeded == 1,
            "a plain task that reports nothing must end Completed at 1/1");
        manager.Shutdown();
    }

    void TestMainThreadPathGivesTheSameResults()
    {
        Journal journal;
        journal.mainThread = std::this_thread::get_id();
        TaskManager manager;
        Check(manager.Initialize(MainThreadOnly()), "a manager without workers must start");
        Check(false == manager.UsesWorkers() && manager.GetWorkerCount() == 0, "it must hold no workers");
        CheckSequentialGroup(manager, journal);
        Check(journal.ranOnMain && false == journal.ranOnWorker, "without workers the bodies run on the main thread");

        Journal loads;
        loads.mainThread = journal.mainThread;
        OwnerPtr<TaskGroup> group = JBro::MakeOwnerPtr<TaskGroup>("load sheets");
        group->Add(JBro::MakeOwnerPtr<SubTaskLoader>(loads, 40, nullptr, 3, 17));
        const TaskGroupId id = manager.Submit(std::move(group));
        Check(manager.FindGroup(id)->GetTaskAt(0).GetState() == TaskState::Pending,
            "without workers submitting must not run the task in place");
        manager.Update();
        Check(loads.finished == 1, "one Update must run the task and deliver its callback");
        Check(loads.lastState == TaskState::Failed && loads.lastSucceeded == 38 && loads.lastFailed == 2,
            "the main-thread path must count the same 38 and 2");
        manager.Shutdown();
    }

    void TestMainThreadBudgetSpreadsTasksAcrossUpdates()
    {
        Journal journal;
        journal.mainThread = std::this_thread::get_id();
        TaskManager manager;
        Check(manager.Initialize(MainThreadOnly(0.0f)), "the manager must start");
        OwnerPtr<TaskGroup> group = JBro::MakeOwnerPtr<TaskGroup>("three");
        for (int i = 0; i < 3; ++i)
        {
            group->Add(JBro::MakeOwnerPtr<SleepTask>(journal, i, 0));
        }
        manager.Submit(std::move(group));
        manager.Update();
        Check(journal.runs.load() == 1, "an exhausted budget must still run one task per Update");
        manager.Update();
        manager.Update();
        Check(journal.runs.load() == 3 && journal.finished == 3, "three Updates must run the three tasks");
        manager.Shutdown();
    }

    void TestCancelEndsTasksAsCanceled(bool workers)
    {
        Journal journal;
        journal.mainThread = std::this_thread::get_id();
        std::atomic<bool> started = false;
        TaskManager manager;
        Check(manager.Initialize(workers ? Workers(2) : MainThreadOnly()), "the manager must start");
        if (workers)
        {
            OwnerPtr<RecordingGroup> group = JBro::MakeOwnerPtr<RecordingGroup>(journal, "cancel", TaskGroupOrder::Sequential);
            group->Add(JBro::MakeOwnerPtr<CancelAwareTask>(journal, started));
            group->Add(JBro::MakeOwnerPtr<SleepTask>(journal, 1, 0));
            const TaskGroupId id = manager.Submit(std::move(group));
            Check(SpinUntil([&] { return started.load(); }), "the first task must start");
            manager.FindGroup(id)->RequestCancel();
            Check(PumpUntilFinished(manager, id), "a canceled group must still finish");
            const TaskGroup& finished = *manager.FindGroup(id);
            Check(finished.GetTaskAt(0).GetState() == TaskState::Canceled,
                "a running task that saw the cancel mark must end Canceled");
            Check(finished.GetTaskAt(0).GetSucceededSubTasks() < 1000, "it must have stopped before its last sub-task");
            Check(finished.GetTaskAt(1).GetState() == TaskState::Canceled, "the task after it must end Canceled");
            Check(journal.runs.load() == 1, "a task canceled before it started must not run its body");
            Check(journal.finished == 2 && journal.groupFinished == 1, "canceled tasks still get their callbacks");
            Check(finished.GetState() == TaskState::Canceled, "the group must read Canceled");
        }
        else
        {
            OwnerPtr<RecordingGroup> group = JBro::MakeOwnerPtr<RecordingGroup>(journal, "cancel", TaskGroupOrder::Parallel);
            group->Add(JBro::MakeOwnerPtr<SleepTask>(journal, 0, 0));
            const TaskGroupId id = manager.Submit(std::move(group));
            manager.FindGroup(id)->RequestCancel();
            manager.Update();
            Check(journal.runs.load() == 0, "without workers a task canceled before Update must not run");
            Check(journal.finished == 1 && journal.lastState == TaskState::Canceled, "it must end Canceled with its callback");
        }
        manager.Shutdown();
    }

    void TestExceptionBecomesFailureReason()
    {
        Journal journal;
        journal.mainThread = std::this_thread::get_id();
        TaskManager manager;
        Check(manager.Initialize(Workers(1)), "the manager must start");
        OwnerPtr<TaskGroup> group = JBro::MakeOwnerPtr<TaskGroup>("throws");
        group->Add(JBro::MakeOwnerPtr<ThrowingTask>(journal));
        Check(manager.Wait(manager.Submit(std::move(group))), "waiting must succeed");
        Check(journal.lastState == TaskState::Failed, "a task that throws must end Failed");
        Check(journal.lastSucceeded == 1 && journal.lastFailed == 1, "the throw must count as the next sub-task failing");
        Check(journal.lastReasonSubTask == 1 && std::strcmp(journal.lastReason, "decoder exploded") == 0,
            "the reason must be what the exception said, at the sub-task it reached");
        manager.Shutdown();
    }

    void TestShutdownWaitsAndDrains(bool workers)
    {
        Journal journal;
        journal.mainThread = std::this_thread::get_id();
        std::atomic<bool> started = false;
        {
            TaskManager manager;
            Check(manager.Initialize(workers ? Workers(1) : MainThreadOnly()), "the manager must start");
            OwnerPtr<RecordingGroup> group = JBro::MakeOwnerPtr<RecordingGroup>(journal, "shutdown", TaskGroupOrder::Parallel);
            group->Add(JBro::MakeOwnerPtr<CancelAwareTask>(journal, started));
            for (int i = 0; i < 3; ++i)
            {
                group->Add(JBro::MakeOwnerPtr<SleepTask>(journal, i, 0));
            }
            manager.Submit(std::move(group));
            if (workers)
            {
                Check(SpinUntil([&] { return started.load(); }), "the long task must start on the one worker");
            }
            manager.Shutdown();
            Check(false == manager.IsInitialized(), "the manager must be down after Shutdown");
            Check(manager.GetGroupCount() == 0, "Shutdown must drop every group");
            Check(journal.running.load() == 0, "no body may still be running when Shutdown returns");
        }
        Check(journal.finished == 4, "Shutdown must deliver every remaining task callback");
        Check(journal.finishedOffMain == 0, "even at Shutdown the callbacks come on the main thread");
        Check(journal.groupFinished == 1, "Shutdown must deliver the group callback");
        Check(journal.runs.load() == (workers ? 1 : 0), "tasks that had not started must end without running");
    }

    void TestSubmitRules()
    {
        Journal journal;
        journal.mainThread = std::this_thread::get_id();
        TaskManager manager;
        Check(manager.Submit(JBro::MakeOwnerPtr<TaskGroup>("early")) == JBro::InvalidTaskGroupId,
            "a manager that has not started must refuse groups");
        Check(manager.Initialize(Workers(1)), "the manager must start");
        Check(false == manager.Initialize(Workers(1)), "a second Initialize must be refused");

        OwnerPtr<RecordingGroup> empty = JBro::MakeOwnerPtr<RecordingGroup>(journal, "empty", TaskGroupOrder::Parallel);
        RecordingGroup* emptyRaw = empty.Get();
        const TaskGroupId emptyId = manager.Submit(std::move(empty));
        Check(emptyId != JBro::InvalidTaskGroupId, "an empty group is still a group");
        Check(journal.groupFinished == 0, "no callback may run inside Submit");
        Check(false == emptyRaw->Add(JBro::MakeOwnerPtr<SleepTask>(journal, 0, 0)), "a submitted group must refuse new tasks");
        manager.Update();
        Check(journal.groupFinished == 1, "an empty group must finish on the next Update");
        Check(manager.FindGroup(emptyId)->GetState() == TaskState::Completed, "an empty group must read Completed");
        Check(false == emptyRaw->Add(OwnerPtr<Task>()), "an empty pointer must be refused");
        manager.Shutdown();
    }

    void TestFinishedGroupsAreTrimmed()
    {
        Journal journal;
        journal.mainThread = std::this_thread::get_id();
        TaskManager manager;
        TaskManagerDesc desc = MainThreadOnly();
        desc.keptFinishedGroups = 2;
        Check(manager.Initialize(desc), "the manager must start");
        TaskGroupId ids[4] = {};
        for (TaskGroupId& id : ids)
        {
            OwnerPtr<TaskGroup> group = JBro::MakeOwnerPtr<TaskGroup>("one");
            group->Add(JBro::MakeOwnerPtr<SleepTask>(journal, 0, 0));
            id = manager.Submit(std::move(group));
        }
        manager.Update();
        Check(manager.GetGroupCount() == 2, "only the newest two finished groups may stay");
        Check(manager.FindGroup(ids[0]) == nullptr && manager.FindGroup(ids[1]) == nullptr, "the oldest ones must go first");
        Check(manager.FindGroup(ids[2]) != nullptr && manager.FindGroup(ids[3]) != nullptr, "the newest ones must stay");
        Check(manager.Wait(ids[0]), "waiting on a group that was trimmed away must return at once");
        manager.Shutdown();
    }

    void TestWorkersCarryNames()
    {
        wchar_t name[64] = {};
        TaskManager manager;
        Check(manager.Initialize(Workers(1)), "the manager must start");
        OwnerPtr<TaskGroup> group = JBro::MakeOwnerPtr<TaskGroup>("name");
        group->Add(JBro::MakeOwnerPtr<NameProbeTask>(name));
        Check(manager.Wait(manager.Submit(std::move(group))), "waiting must succeed");
        Check(std::wcscmp(name, L"JBro Task Worker 1") == 0, "a worker must carry a name the debugger can show");
        manager.Shutdown();
    }

    class SubmitFromCallbackTask final : public Task
    {
    public:
        SubmitFromCallbackTask(TaskManager& manager, Journal& journal, TaskGroupId& next)
            : Task("chain")
            , m_manager(manager)
            , m_journal(journal)
            , m_next(next)
        {
        }

    protected:
        void Run() override
        {
        }

        void OnFinished(const TaskResult& result) override
        {
            RecordFinished(m_journal, result);
            // 콜백 안에서 다음 묶음을 제출하는 것이 순서를 잇는 길이다. 그 안에서 기다리는 것은 막힌다.
            OwnerPtr<TaskGroup> group = JBro::MakeOwnerPtr<TaskGroup>("next");
            group->Add(JBro::MakeOwnerPtr<SleepTask>(m_journal, 1, 0));
            m_next = m_manager.Submit(std::move(group));
            m_waitRefused = false == m_manager.Wait(m_next);
        }

    public:
        bool m_waitRefused = false;

    private:
        TaskManager& m_manager;
        Journal& m_journal;
        TaskGroupId& m_next;
    };

    void TestCallbackMaySubmitButNotWait()
    {
        Journal journal;
        journal.mainThread = std::this_thread::get_id();
        TaskManager manager;
        Check(manager.Initialize(Workers(2)), "the manager must start");
        TaskGroupId next = JBro::InvalidTaskGroupId;
        OwnerPtr<TaskGroup> group = JBro::MakeOwnerPtr<TaskGroup>("first");
        group->Add(JBro::MakeOwnerPtr<SubmitFromCallbackTask>(manager, journal, next));
        const TaskGroupId first = manager.Submit(std::move(group));
        Check(manager.Wait(first), "waiting on the first group must succeed");
        const auto* chain = static_cast<const SubmitFromCallbackTask*>(&manager.FindGroup(first)->GetTaskAt(0));
        Check(chain->m_waitRefused, "waiting inside a callback must be refused, not hang");
        Check(next != JBro::InvalidTaskGroupId, "a callback must be able to submit the next group");
        Check(manager.Wait(next) && journal.finished == 2, "the group submitted from a callback must run and finish");
        manager.Shutdown();
    }

    class FlagTask final : public Task
    {
    public:
        FlagTask(int& finished, std::thread::id& callbackThread)
            : Task("flag")
            , m_finished(finished)
            , m_callbackThread(callbackThread)
        {
        }

    protected:
        void Run() override
        {
        }

        void OnFinished(const TaskResult&) override
        {
            ++m_finished;
            m_callbackThread = std::this_thread::get_id();
        }

    private:
        int& m_finished;
        std::thread::id& m_callbackThread;
    };

    // 엔진이 관리자를 들고 Tick 첫머리에서 콜백을 부르고, 내릴 때 남은 콜백을 비우는지 본다.
    void TestEngineOwnsTheManager()
    {
        JBro::WindowsPlatform platform;
        JBro::D3D12RHIModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "platform must initialize for the engine task test");
        if (false == rhi.Initialize(memory))
        {
            std::cout << "  [skip] no D3D12 device; engine task manager wiring not exercised" << std::endl;
            platform.Shutdown();
            return;
        }
        JBro::EngineConfig config;
        config.window.visible = false;
        config.window.width = 64;
        config.window.height = 64;
        config.audioEnabled = false;
        config.networkEnabled = false;
        config.tasks.workerCount = 2;
        int finished = 0;
        std::thread::id callbackThread;
        {
            JBro::EngineInstance engine;
            Check(nullptr == engine.GetTaskManager(), "a host that has not started holds no task manager");
            Check(engine.Initialize(config, platform, rhi), "the host must initialize");
            TaskManager* manager = engine.GetTaskManager();
            Check(manager != nullptr && manager->GetWorkerCount() == 2, "the host must start the manager from its config");
            OwnerPtr<TaskGroup> group = JBro::MakeOwnerPtr<TaskGroup>("engine");
            group->Add(JBro::MakeOwnerPtr<FlagTask>(finished, callbackThread));
            const TaskGroupId id = manager->Submit(std::move(group));
            Check(SpinUntil([&] {
                Check(engine.Tick(1.0f / 60.0f), "the host must keep ticking");
                return finished == 1;
            }), "a Tick must deliver the finished task's callback");
            Check(callbackThread == std::this_thread::get_id(), "the host must deliver it on the main thread");
            Check(manager->FindGroup(id)->IsFinished(), "the group must be finished after that Tick");

            OwnerPtr<TaskGroup> late = JBro::MakeOwnerPtr<TaskGroup>("late");
            late->Add(JBro::MakeOwnerPtr<FlagTask>(finished, callbackThread));
            manager->Submit(std::move(late));
            engine.Shutdown();
            Check(finished == 2, "shutting the host down must deliver the callbacks still owed");
            Check(nullptr == engine.GetTaskManager(), "a host that has shut down holds no task manager");
        }
        rhi.Shutdown();
        platform.Shutdown();
    }
}

int RunTaskManagerTests()
{
    try
    {
        TestParallelGroupRunsOnSeveralWorkersAtOnce();
        TestSequentialGroupNeverOverlaps();
        TestProgressCountsUpAndFailuresSeparately();
        TestUnreportedSubTasksCountAsDone();
        TestMainThreadPathGivesTheSameResults();
        TestMainThreadBudgetSpreadsTasksAcrossUpdates();
        TestCancelEndsTasksAsCanceled(true);
        TestCancelEndsTasksAsCanceled(false);
        TestExceptionBecomesFailureReason();
        TestShutdownWaitsAndDrains(true);
        TestShutdownWaitsAndDrains(false);
        TestSubmitRules();
        TestFinishedGroupsAreTrimmed();
        TestWorkersCarryNames();
        TestCallbackMaySubmitButNotWait();
        TestEngineOwnsTheManager();
    }
    catch (const std::exception& error)
    {
        std::cout << "task manager tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "task manager tests passed" << std::endl;
    return 0;
}
