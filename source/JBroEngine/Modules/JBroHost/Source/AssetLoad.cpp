#include <JBro/Host/AssetLoad.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Task/TaskManager.h>

#include <utility>

namespace JBro
{
    namespace
    {
        String FileNameOf(const String& path)
        {
            const std::size_t slash = path.find_last_of("/\\");
            return slash == String::npos ? path : String(path.substr(slash + 1));
        }

        // 에셋 하나를 워커에서 디코드한다. 본문은 에셋 시스템을 보지 않는다 - 준비한 경로와 옵션, 플랫폼의 파일 읽기만 쓴다.
        class AssetDecodeTask final : public Task
        {
        public:
            AssetDecodeTask(AssetDecodeJob&& job, AssetSystem& assets, const IAssetSource& source, AssetLoadResult& result)
                : Task(FileNameOf(job.record.relativePath))
                , m_job(std::move(job))
                , m_assets(assets)
                , m_source(source)
                , m_result(result)
            {
            }

        protected:
            void Run() override
            {
                if (false == DecodeAssetFile(m_source, m_job))
                {
                    FailSubTask(0, m_job.failure.c_str());
                }
            }

            void OnFinished(const TaskResult& result) override
            {
                if (result.state == TaskState::Canceled)
                {
                    return;
                }
                // 실패도 여기로 보낸다. 풀에 넣지 않고 사유를 로그에 남긴다(워커는 로그를 남기지 않는다).
                const AssetHandle handle = m_assets.AdoptDecoded(m_job);
                if (handle.generation != 0)
                {
                    m_result.held.Add(handle);
                    return;
                }
                if (m_result.failed == 0)
                {
                    m_result.firstFailure = m_job.failure.empty() ? m_job.record.relativePath : m_job.failure;
                }
                ++m_result.failed;
            }

        private:
            AssetDecodeJob m_job;
            AssetSystem& m_assets;
            const IAssetSource& m_source;
            AssetLoadResult& m_result;
        };

        class AssetLoadGroup final : public TaskGroup
        {
        public:
            AssetLoadGroup(const char* nameKey, AssetLoadResult& result)
                : TaskGroup(nameKey != nullptr ? nameKey : "", TaskGroupOrder::Parallel)
                , m_result(result)
            {
            }

        protected:
            void OnFinished() override
            {
                m_result.canceled = GetState() == TaskState::Canceled;
                m_result.finished = true;
            }

        private:
            AssetLoadResult& m_result;
        };
    }

    namespace
    {
        // 미룬 디코드 하나(D-455). 결과는 끝난 뒤 메인에서 미룬 자리들에 채운다 - 실패해도 채운다(빈 자료로 끝나 그리는 쪽이 건너뛴다).
        class DeferredDecodeTask final : public Task
        {
        public:
            DeferredDecodeTask(AssetDecodeJob&& job, AssetSystem& assets, const IAssetSource& source)
                : Task(FileNameOf(job.record.relativePath))
                , m_job(std::move(job))
                , m_assets(assets)
                , m_source(source)
            {
            }

        protected:
            void Run() override
            {
                if (false == DecodeAssetFile(m_source, m_job))
                {
                    FailSubTask(0, m_job.failure.c_str());
                }
            }

            void OnFinished(const TaskResult& result) override
            {
                // 멈춘 것은 에셋 시스템을 풀거나 내리는 중이다 - 자리도 함께 사라진다.
                if (result.state == TaskState::Canceled)
                {
                    return;
                }
                m_assets.AdoptDecoded(m_job);
            }

        private:
            AssetDecodeJob m_job;
            AssetSystem& m_assets;
            const IAssetSource& m_source;
        };
    }

    void SubmitDeferredAssetDecodes(TaskManager& tasks, AssetSystem& assets, const char* groupNameKey, Array<TaskGroupId>& groups)
    {
        for (std::size_t index = groups.Size(); index > 0; --index)
        {
            const TaskGroup* group = tasks.FindGroup(groups[index - 1]);
            if (group == nullptr || group->IsFinished())
            {
                groups.RemoveAt(index - 1);
            }
        }
        const IAssetSource* source = assets.GetSource();
        if (false == tasks.IsInitialized() || source == nullptr || false == assets.IsDeferredDecode())
        {
            return;
        }
        Array<AssetDecodeJob> jobs;
        if (assets.TakeDeferredJobs(jobs) == 0)
        {
            return;
        }
        OwnerPtr<TaskGroup> group = MakeOwnerPtr<TaskGroup>(String(groupNameKey != nullptr ? groupNameKey : ""), TaskGroupOrder::Parallel);
        for (std::size_t index = 0; index < jobs.Size(); ++index)
        {
            group->Add(MakeOwnerPtr<DeferredDecodeTask>(std::move(jobs[index]), assets, *source));
        }
        const TaskGroupId id = tasks.Submit(std::move(group));
        if (id != InvalidTaskGroupId)
        {
            groups.Add(id);
        }
    }

    void WaitDeferredAssetDecodes(TaskManager& tasks, Array<TaskGroupId>& groups)
    {
        if (false == tasks.IsInitialized())
        {
            groups.Clear();
            return;
        }
        for (std::size_t index = 0; index < groups.Size(); ++index)
        {
            if (TaskGroup* group = tasks.FindGroup(groups[index]))
            {
                group->RequestCancel();
            }
        }
        for (std::size_t index = 0; index < groups.Size(); ++index)
        {
            tasks.Wait(groups[index]);
        }
        groups.Clear();
    }

    TaskGroupId SubmitAssetLoad(TaskManager& tasks, AssetSystem& assets,
        ArrayView<const AssetId> ids, const char* groupNameKey, AssetLoadResult& result)
    {
        result = AssetLoadResult{};
        if (false == tasks.IsInitialized())
        {
            result.finished = true;
            return InvalidTaskGroupId;
        }
        OwnerPtr<AssetLoadGroup> group = MakeOwnerPtr<AssetLoadGroup>(groupNameKey, result);
        const IAssetSource* source = assets.GetSource();
        // 스프라이트 둘이 한 텍스처를 쓰면 준비한 아이디가 겹친다. 한 번만 보낸다.
        Array<AssetId> prepared;
        for (std::size_t index = 0; index < ids.Size(); ++index)
        {
            AssetDecodeJob job;
            if (source == nullptr || false == assets.PrepareDecode(ids[index], job) || prepared.Contains(job.id))
            {
                continue;
            }
            prepared.Add(job.id);
            group->Add(MakeOwnerPtr<AssetDecodeTask>(std::move(job), assets, *source, result));
        }
        return tasks.Submit(std::move(group));
    }
}
