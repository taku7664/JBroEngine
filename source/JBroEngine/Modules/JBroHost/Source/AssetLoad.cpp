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
            AssetDecodeTask(AssetDecodeJob&& job, AssetSystem& assets, IPlatform& platform, AssetLoadResult& result)
                : Task(FileNameOf(job.sourcePath))
                , m_job(std::move(job))
                , m_assets(assets)
                , m_platform(platform)
                , m_result(result)
            {
            }

        protected:
            void Run() override
            {
                if (false == DecodeAssetFile(m_platform, m_job))
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
                    m_result.firstFailure = m_job.failure.empty() ? m_job.sourcePath : m_job.failure;
                }
                ++m_result.failed;
            }

        private:
            AssetDecodeJob m_job;
            AssetSystem& m_assets;
            IPlatform& m_platform;
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

    TaskGroupId SubmitAssetLoad(TaskManager& tasks, AssetSystem& assets, IPlatform& platform,
        ArrayView<const AssetId> ids, const char* groupNameKey, AssetLoadResult& result)
    {
        result = AssetLoadResult{};
        if (false == tasks.IsInitialized())
        {
            result.finished = true;
            return InvalidTaskGroupId;
        }
        OwnerPtr<AssetLoadGroup> group = MakeOwnerPtr<AssetLoadGroup>(groupNameKey, result);
        // 스프라이트 둘이 한 텍스처를 쓰면 준비한 아이디가 겹친다. 한 번만 보낸다.
        Array<AssetId> prepared;
        for (std::size_t index = 0; index < ids.Size(); ++index)
        {
            AssetDecodeJob job;
            if (false == assets.PrepareDecode(ids[index], job) || prepared.Contains(job.id))
            {
                continue;
            }
            prepared.Add(job.id);
            group->Add(MakeOwnerPtr<AssetDecodeTask>(std::move(job), assets, platform, result));
        }
        return tasks.Submit(std::move(group));
    }
}
