#include "WorkerPool.h"

#include <algorithm>
#include <cwchar>

#if defined(_WIN32)
// windows.h 의 min·max 매크로가 std::min 을 깨지 않게 한다(프로젝트 설정 밖에서 빌드해도).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace JBro::Physics2D::Internal
{
    namespace
    {
        // 디버거의 스레드 목록에서 물리 워커를 알아보게 한다. 이름을 붙이지 못해도 워커는 그대로 돈다.
        void NameWorkerThread(std::uint32_t index)
        {
#if defined(_WIN32)
            wchar_t name[32];
            std::swprintf(name, 32, L"JBro Physics Worker %u", index + 1);
            (void)SetThreadDescription(GetCurrentThread(), name);
#else
            (void)index;
#endif
        }
    }

    WorkerPool::~WorkerPool()
    {
        Stop();
    }

    void WorkerPool::Start(std::uint32_t workerCount)
    {
        Stop();
#if defined(__EMSCRIPTEN__)
        // 웹은 스레드가 없다. 값이 무엇이든 부르는 스레드 하나로 돈다.
        workerCount = 0;
#endif
        workerCount = std::min(workerCount, MaxWorkers);
        std::uint64_t startGeneration = 0;
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = false;
            startGeneration = m_generation;
        }
        for (std::uint32_t i = 0; i < workerCount; ++i)
        {
            // 늦게 뜬 워커도 시작 때의 세대를 기준으로 삼는다. 그 사이 일이 왔으면 바로 끼어들어 제 몫을 센다.
            m_threads[i] = std::thread([this, i, startGeneration]()
            {
                NameWorkerThread(i);
                std::uint64_t seen = startGeneration;
                for (;;)
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_wake.wait(lock, [this, &seen]()
                    {
                        return m_stopping || m_generation != seen;
                    });
                    if (m_stopping)
                    {
                        return;
                    }
                    seen = m_generation;
                    lock.unlock();
                    RunChunks();
                    lock.lock();
                    --m_busyWorkers;
                    if (m_busyWorkers == 0)
                    {
                        m_done.notify_one();
                    }
                }
            });
            ++m_workerCount;
        }
    }

    void WorkerPool::Stop()
    {
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }
        m_wake.notify_all();
        for (std::uint32_t i = 0; i < m_workerCount; ++i)
        {
            m_threads[i].join();
        }
        m_workerCount = 0;
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = false;
    }

    std::uint32_t WorkerPool::GetWorkerCount() const
    {
        return m_workerCount;
    }

    void WorkerPool::ParallelFor(std::uint32_t count, std::uint32_t grain, Job job, void* context)
    {
        if (count == 0)
        {
            return;
        }
        grain = std::max(grain, 1u);
        if (m_workerCount == 0 || count <= grain)
        {
            job(context, 0, count);
            return;
        }
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_job = job;
            m_context = context;
            m_count = count;
            m_grain = grain;
            m_next.store(0, std::memory_order_relaxed);
            m_busyWorkers = m_workerCount;
            ++m_generation;
        }
        m_wake.notify_all();
        RunChunks();
        std::unique_lock<std::mutex> lock(m_mutex);
        m_done.wait(lock, [this]()
        {
            return m_busyWorkers == 0;
        });
    }

    void WorkerPool::RunChunks()
    {
        for (;;)
        {
            const std::uint32_t begin = m_next.fetch_add(m_grain, std::memory_order_relaxed);
            if (begin >= m_count)
            {
                return;
            }
            const std::uint32_t end = std::min(begin + m_grain, m_count);
            m_job(m_context, begin, end);
        }
    }
}
