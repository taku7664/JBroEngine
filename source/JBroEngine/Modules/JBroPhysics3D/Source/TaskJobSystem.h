#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemWithBarrier.h>

#include <JBro/Task/WorkerPool.h>
#include <JBro/Types/UInt.h>

// **Jolt 의 잡을 엔진의 워커 묶음(`JBro::WorkerPool`) 위에서 돌린다**(D-428). Jolt 의 자기 스레드 풀(`JobSystemThreadPool`) 자리다 -
// 워커 스레드는 엔진의 태스크 모듈이 들고, Jolt 는 잡을 만들고 장벽에서 기다리는 것만 한다.
//
// 잡은 미리 잡은 자유 목록에서 꺼낸다(스텝 중 할당 없음). 줄 세울 때 참조를 하나 더하고 워커가 돌린 뒤 놓는다(Jolt 의 풀과 같은 약속).
// 장벽을 기다리는 스레드는 장벽에 든 잡을 스스로도 돌린다(`JobSystemWithBarrier`) - 메인 스레드도 일꾼 하나다.
namespace JBro::Physics3D
{
    class TaskJobSystem final : public JPH::JobSystemWithBarrier
    {
    public:
        TaskJobSystem(UInt32 maxJobs, UInt32 maxBarriers);
        ~TaskJobSystem() override;

        // 워커 수를 바꾼다. 스텝 밖에서만 부른다(돌고 있는 잡이 없어야 한다).
        void SetWorkerCount(UInt32 count);
        UInt32 GetWorkerCount() const;

        int GetMaxConcurrency() const override;
        JobHandle CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function, JPH::uint32 dependencies = 0) override;
        void CreateJobs(const char* name, JPH::ColorArg color, const JobFunction& function, JobHandle* jobs, JPH::uint32 count,
            JPH::uint32 dependencies = 0) override;
        void FreeJob(Job* job) override;
        void QueueJob(Job* job) override;
        void QueueJobs(Job** jobs, JPH::uint count) override;

    private:
        static void RunJob(void* context);
        Job* AllocateJob(const char* name, JPH::ColorArg color, const JobFunction& function, JPH::uint32 dependencies);

        JPH::FixedSizeFreeList<Job> m_jobs;
        WorkerPool m_workers;
        UInt32 m_capacity = 0;
    };
}
