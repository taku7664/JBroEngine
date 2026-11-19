#include <JBro/Task/WorkerPool.h>

namespace JBro
{
    WorkerPool::~WorkerPool()
    {
        Stop();
    }

    Bool WorkerPool::Start(UInt32 workerCount, UInt32 capacity)
    {
        Stop();
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
            m_workers.Emplace([this] { WorkerLoop(); });
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

    void WorkerPool::WorkerLoop()
    {
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
