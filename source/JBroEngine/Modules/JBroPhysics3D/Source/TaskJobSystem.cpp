#include "TaskJobSystem.h"

namespace JBro::Physics3D
{
    TaskJobSystem::TaskJobSystem(UInt32 maxJobs, UInt32 maxBarriers) : m_capacity(maxJobs)
    {
        JobSystemWithBarrier::Init(maxBarriers);
        m_jobs.Init(maxJobs, maxJobs);
    }

    TaskJobSystem::~TaskJobSystem()
    {
        // 워커가 줄 선 잡을 다 돌린 뒤 거둔다 - 그 뒤에 자유 목록이 사라진다.
        m_workers.Stop();
    }

    void TaskJobSystem::SetWorkerCount(UInt32 count)
    {
        m_workers.Start(count, m_capacity);
    }

    UInt32 TaskJobSystem::GetWorkerCount() const
    {
        return m_workers.GetWorkerCount();
    }

    int TaskJobSystem::GetMaxConcurrency() const
    {
        return static_cast<int>(m_workers.GetWorkerCount().Get()) + 1;
    }

    JPH::JobSystem::Job* TaskJobSystem::AllocateJob(const char* name, JPH::ColorArg color, const JobFunction& function, JPH::uint32 dependencies)
    {
        // 자리가 모자라면 단언한다 - 자리는 `cMaxPhysicsJobs` 로 잡아 스텝 하나가 넘지 않는다(Jolt 의 풀도 같은 수다).
        const JPH::uint32 index = m_jobs.ConstructObject(name, color, this, function, dependencies);
        JPH_ASSERT(index != JPH::FixedSizeFreeList<Job>::cInvalidObjectIndex, "the physics job table is full");
        return &m_jobs.Get(index);
    }

    void TaskJobSystem::CreateJobs(const char* name, JPH::ColorArg color, const JobFunction& function, JobHandle* jobs, JPH::uint32 count,
        JPH::uint32 dependencies)
    {
        for (JPH::uint32 index = 0; index < count; ++index)
        {
            jobs[index] = JobHandle(AllocateJob(name, color, function, dependencies));
        }
        if (dependencies == 0)
        {
            static_assert(sizeof(JobHandle) == sizeof(Job*), "a job handle is one pointer, as Jolt's own pool assumes");
            QueueJobs(reinterpret_cast<Job**>(jobs), count);
        }
    }

    JPH::JobHandle TaskJobSystem::CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function, JPH::uint32 dependencies)
    {
        Job* job = AllocateJob(name, color, function, dependencies);
        JobHandle handle(job);
        if (dependencies == 0)
        {
            QueueJob(job);
        }
        return handle;
    }

    void TaskJobSystem::FreeJob(Job* job)
    {
        m_jobs.DestructObject(job);
    }

    void TaskJobSystem::QueueJob(Job* job)
    {
        // 워커가 놓을 참조다. 줄에 넣지 못하면(워커 없음·자리 참) 이 스레드에서 바로 돌린다.
        job->AddRef();
        if (false == m_workers.TrySubmit(WorkerJob{&TaskJobSystem::RunJob, job}))
        {
            RunJob(job);
        }
    }

    void TaskJobSystem::QueueJobs(Job** jobs, JPH::uint count)
    {
        for (JPH::uint index = 0; index < count; ++index)
        {
            QueueJob(jobs[index]);
        }
    }

    void TaskJobSystem::RunJob(void* context)
    {
        Job* job = static_cast<Job*>(context);
        job->Execute();
        job->Release();
    }
}
