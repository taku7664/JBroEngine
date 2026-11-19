#pragma once

#include <JBro/Types/Array.h>

#include <condition_variable>
#include <mutex>
#include <thread>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

// **나눴다 합치는 짧은 일의 워커 묶음**이다(D-428). 한 프레임(한 물리 스텝) 안에서 일을 잘게 나눠 돌리고 그 자리에서 기다려 합치는 쓰임이다 -
// 3D 물리가 Jolt 의 잡을, 2D 물리가 좁은 판정과 색칠한 접촉 풀이를 이 위에서 돌린다(D-429).
//
// `TaskManager` 와 갈리는 점: 어느 스레드에서나(워커 안에서도) 일을 넣을 수 있고, 넣을 때 할당하지 않으며(자리는 `Start` 에서 한 번 잡는다),
// 끝을 메인 스레드로 알리는 콜백이 없다 - 기다림은 일을 넣은 쪽이 제 방식(장벽·카운터)으로 한다. 로딩처럼 오래 걸리고 콜백이 필요한 일은 `TaskManager` 다.
//
// **스레드 계약**: 일은 워커에서 돈다. 그 안에서 `SafePtr`·`OwnerPtr`·`Ref<T>`·`Handle::GameObject` 를 만들거나 복사하거나 파괴하지 않는다.
// 일 자체는 함수 포인터와 문맥 포인터 둘이다(`std::function` 을 받지 않는다 - 캡처가 워커에서 소멸하지 않게). `Start`·`Stop` 은 한 스레드(주인)에서만 부른다.
namespace JBro
{
    struct WorkerJob
    {
        void (*run)(void* context) = nullptr;
        void* context = nullptr;
    };

    class WorkerPool
    {
    public:
        WorkerPool() = default;
        // `Stop` 을 부른다.
        ~WorkerPool();
        WorkerPool(const WorkerPool&) = delete;
        WorkerPool& operator=(const WorkerPool&) = delete;

        // 워커 workerCount 개를 세우고, 한꺼번에 줄 설 수 있는 일 capacity 개의 자리를 잡는다(2 의 거듭제곱으로 올린다).
        // 이미 서 있으면 멈춘 뒤 다시 세운다. 0 개면 워커 없이 서고 `TrySubmit` 은 늘 거짓이다(부른 쪽이 직접 돌린다).
        // 스레드가 없는 빌드(웹)는 늘 0 개다. threadName 을 주면 디버거의 스레드 목록에 "이름 n" 으로 보인다.
        Bool Start(UInt32 workerCount, UInt32 capacity, const wchar_t* threadName = nullptr);
        // 줄 선 일을 워커가 모두 돌린 뒤 워커를 거둔다.
        void Stop();
        UInt32 GetWorkerCount() const;

        // 일 하나를 줄 세운다. 어느 스레드에서나 부르고 할당하지 않는다. 워커가 없거나 자리가 차면 거짓이다 - 부른 쪽이 그 자리에서 돌린다.
        Bool TrySubmit(const WorkerJob& job);

        // **[0, count) 를 grain 개씩 잘라 워커와 부르는 스레드가 나눠 돌고, 모두 끝나야 돌아온다**(D-429). 한 칸은 한 번만 받는다.
        // 워커가 없거나 한 덩어리면 부르는 스레드에서 차례로 돈다. 할당하지 않는다 - 나눌 상태는 부른 쪽 스택에 있고 돕는 일 수만큼 줄에 넣는다.
        using RangeJob = void (*)(void* context, UInt32 begin, UInt32 end);
        void ParallelFor(UInt32 count, UInt32 grain, RangeJob job, void* context);

    private:
        struct Range;
        static void HelpRange(void* context);
        static void RunChunks(Range& range);
        void WorkerLoop(UInt32 index);

        std::mutex m_mutex;
        std::condition_variable m_wake;
        // 고리 큐다. `m_mutex` 아래에서만 만진다.
        Array<WorkerJob> m_jobs;
        UInt32 m_mask = 0;
        UInt32 m_head = 0;
        UInt32 m_count = 0;
        Bool m_stopping = false;
        // `ParallelFor` 의 끝 알림이다. 돕는 일은 잠금 아래에서 제 몫을 내리고 이것을 깨운다 - 부른 쪽은 같은 잠금 아래에서 보므로 스택의 상태가 먼저 사라지지 않는다.
        std::condition_variable m_rangeDone;
        Array<std::thread> m_workers;
        const wchar_t* m_threadName = nullptr;
    };
}
