#include <JBro/Task/WorkerPool.h>

#include <atomic>
#include <cstdint>
#include <cwchar>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace JBro
{
    namespace
    {
        // 디버거의 스레드 목록에서 워커를 알아보게 한다. 이름을 붙이지 못해도 워커는 그대로 돈다.
        void NameThread(const wchar_t* name, UInt32 index)
        {
#if defined(_WIN32)
            if (name != nullptr)
            {
                wchar_t full[64];
                std::swprintf(full, 64, L"%ls %u", name, (index + 1u).Get());
                (void)SetThreadDescription(GetCurrentThread(), full);
            }
#else
            (void)name;
            (void)index;
#endif
        }
    }

    // `ParallelFor` 한 번의 나눌 상태다. 부른 쪽 스택에 있다.
    struct WorkerPool::Range
    {
        WorkerPool* pool = nullptr;
        RangeJob job = nullptr;
        void* context = nullptr;
        std::uint32_t count = 0;
        std::uint32_t grain = 1;
        std::atomic<std::uint32_t> next{0};
        // 아직 끝나지 않은 돕는 일. 풀의 잠금 아래에서만 내린다.
        std::uint32_t helpers = 0;
    };

    WorkerPool::~WorkerPool()
    {
        Stop();
    }

    Bool WorkerPool::Start(UInt32 workerCount, UInt32 capacity, const wchar_t* threadName)
    {
        Stop();
#if defined(__EMSCRIPTEN__)
        // 웹은 스레드가 없다. 값이 무엇이든 부르는 스레드 하나로 돈다.
        workerCount = 0;
#endif
        m_threadName = threadName;
        UInt32 size = 1;
        while (size < capacity && size < 0x80000000u)
        {
            size = size * 2u;
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_jobs.Clear();
            m_jobs.Resize(size);
            m_mask = size - 1u;
            m_head = 0;
            m_count = 0;
            m_stopping = false;
        }
        m_workers.Reserve(workerCount);
        for (UInt32 index = 0; index < workerCount; ++index)
        {
            m_workers.Emplace([this, index] { WorkerLoop(index); });
        }
        return true;
    }

    void WorkerPool::Stop()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }
        m_wake.notify_all();
        for (std::thread& worker : m_workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        m_workers.Clear();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = false;
    }

    UInt32 WorkerPool::GetWorkerCount() const
    {
        return static_cast<UInt32>(m_workers.Size());
    }

    Bool WorkerPool::TrySubmit(const WorkerJob& job)
    {
        if (job.run == nullptr || m_workers.Size() == 0)
        {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_count == m_jobs.Size() || m_stopping)
            {
                return false;
            }
            m_jobs[(m_head + m_count) & m_mask] = job;
            m_count = m_count + 1u;
        }
        m_wake.notify_one();
        return true;
    }

    void WorkerPool::ParallelFor(UInt32 count, UInt32 grain, RangeJob job, void* context)
    {
        if (count == 0u || job == nullptr)
        {
            return;
        }
        grain = grain > 0u ? grain : UInt32(1u);
        const UInt32 chunks = (count + grain - 1u) / grain;
        if (m_workers.Size() == 0 || chunks <= 1u)
        {
            job(context, 0, count);
            return;
        }
        Range range;
        range.pool = this;
        range.job = job;
        range.context = context;
        range.count = count;
        range.grain = grain;
        // 부르는 스레드도 한 몫이라 덩어리 수 - 1 까지만 돕게 한다.
        const UInt32 wanted = chunks - 1u < static_cast<UInt32>(m_workers.Size()) ? chunks - 1u : static_cast<UInt32>(m_workers.Size());
        for (UInt32 index = 0; index < wanted; ++index)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                range.helpers = range.helpers + 1u;
            }
            if (false == TrySubmit(WorkerJob{&WorkerPool::HelpRange, &range}))
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                range.helpers = range.helpers - 1u;
                break;
            }
        }
        RunChunks(range);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_rangeDone.wait(lock, [&range] { return range.helpers == 0u; });
    }

    void WorkerPool::HelpRange(void* context)
    {
        Range& range = *static_cast<Range*>(context);
        RunChunks(range);
        WorkerPool& pool = *range.pool;
        {
            std::lock_guard<std::mutex> lock(pool.m_mutex);
            range.helpers = range.helpers - 1u;
        }
        // 잠금을 놓은 뒤에는 `range` 를 만지지 않는다 - 부른 쪽이 돌아가 스택에서 사라질 수 있다. 알림은 풀의 것이다.
        pool.m_rangeDone.notify_all();
    }

    void WorkerPool::RunChunks(Range& range)
    {
        for (;;)
        {
            const std::uint32_t begin = range.next.fetch_add(range.grain, std::memory_order_relaxed);
            if (begin >= range.count)
            {
                return;
            }
            const std::uint32_t end = begin + range.grain < range.count ? begin + range.grain : range.count;
            range.job(range.context, begin, end);
        }
    }

    void WorkerPool::WorkerLoop(UInt32 index)
    {
        NameThread(m_threadName, index);
        for (;;)
        {
            WorkerJob job;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_wake.wait(lock, [this] { return m_count > 0u || m_stopping; });
                // 멈추라고 해도 줄 선 일은 다 돌린다 - 넣은 쪽이 그 끝을 기다리고 있을 수 있다.
                if (m_count == 0u)
                {
                    return;
                }
                job = m_jobs[m_head];
                m_head = (m_head + 1u) & m_mask;
                m_count = m_count - 1u;
            }
            job.run(job.context);
        }
    }
}
