#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

// 물리 전용 워커 풀이다(D-223). 모듈마다 스레드를 두지 않는다는 규약(D-209)의 예외로, 스텝 안에서 나눴다가 그 자리에서
// 합치는 일(좁은 판정)에만 쓴다 - `TaskManager` 는 제출마다 할당하고 결과가 다음 프레임에 와서 이 일에 맞지 않는다.
//
// 워커 수는 시작할 때 정하고, 도는 동안 할당하지 않는다. 부르는 스레드(메인)도 일을 나눠 맡는다. 스레드가 없는 빌드(웹)에서는
// 워커를 세우지 않고 `ParallelFor` 가 부르는 스레드에서 차례로 돈다.
namespace JBro::Physics2D::Internal
{
    class WorkerPool
    {
    public:
        // 명시한 값이 이보다 크면 여기서 자른다.
        static constexpr UInt32 MaxWorkers = 16;

        using Job = void (*)(void* context, UInt32 begin, UInt32 end);

        WorkerPool() = default;
        ~WorkerPool();
        WorkerPool(const WorkerPool&) = delete;
        WorkerPool& operator=(const WorkerPool&) = delete;

        // 워커를 다시 세운다. 0 이면 워커 없이 부르는 스레드만 쓴다.
        void Start(UInt32 workerCount);
        void Stop();
        UInt32 GetWorkerCount() const;

        // [0, count) 를 grain 개씩 잘라 워커와 부르는 스레드가 나눠 돈다. 모두 끝나야 돌아온다. job 은 같은 칸을 두 번 받지 않는다.
        void ParallelFor(UInt32 count, UInt32 grain, Job job, void* context);

    private:
        void RunChunks();

        std::thread             m_threads[MaxWorkers];
        UInt32           m_workerCount = 0;

        std::mutex              m_mutex;
        std::condition_variable m_wake;
        std::condition_variable m_done;
        UInt64           m_generation = 0;
        Bool                    m_stopping = false;

        // 지금 도는 일. 부르는 스레드가 `m_generation` 을 올리기 전에 채우고, 모두 끝날 때까지 바꾸지 않는다.
        Job                        m_job = nullptr;
        void*                      m_context = nullptr;
        UInt32              m_count = 0;
        UInt32              m_grain = 1;
        std::atomic<std::uint32_t> m_next{ 0 };
        UInt32              m_busyWorkers = 0;
    };
}
