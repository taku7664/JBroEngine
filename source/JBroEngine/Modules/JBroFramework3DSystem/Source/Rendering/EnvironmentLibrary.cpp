#include <JBro/Framework3DSystem/Rendering/EnvironmentLibrary.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Core/Log.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Task/TaskManager.h>

#include <JBro/Types/Int.h>

#include <thread>
#include <utility>

namespace JBro
{
    // **워커에서 하늘 맵을 굽는다**(D-449). 원본은 떠 온 제 사본이라 그사이 에셋이 다시 읽혀도 상관없다. 결과는 제 멤버에 담고,
    // 라이브러리에 넘기는 것은 `OnFinished`(메인 스레드)다. 라이브러리는 내려가기 전에 이 태스크가 끝나기를 기다린다.
    class EnvironmentBakeTask final : public Task
    {
    public:
        EnvironmentBakeTask(EnvironmentLibrary& library, UInt32 serial, UInt32 width, UInt32 height, Array<Float>&& rgb)
            : Task(String("Bake a sky map"))
            , m_library(library)
            , m_serial(serial)
            , m_width(width)
            , m_height(height)
            , m_rgb(std::move(rgb))
        {
        }

    protected:
        void Run() override
        {
            EnvironmentMapCancel cancel;
            cancel.requested = &EnvironmentBakeTask::CancelRequested;
            cancel.context = this;
            m_built = BuildEnvironmentMap(m_rgb.Data(), m_width, m_height, m_build, cancel);
            // 원본은 여기서 놓는다 - 큰 하늘은 수백 MB 다.
            m_rgb = Array<Float>{};
        }

        void OnFinished(const TaskResult& result) override
        {
            // 멈추라고 한 뒤에 돌아온 것은 끝까지 구웠어도 버린다 - 라이브러리는 이미 다른 하늘을 바란다.
            const Bool completed = result.state == TaskState::Completed && false == IsCancelRequested();
            m_library.FinishBake(m_serial, true, completed, m_built, std::move(m_build));
        }

    private:
        static Bool CancelRequested(const void* context)
        {
            return static_cast<const EnvironmentBakeTask*>(context)->IsCancelRequested();
        }

        EnvironmentLibrary& m_library;
        UInt32 m_serial = 0;
        UInt32 m_width = 0;
        UInt32 m_height = 0;
        Array<Float> m_rgb;
        EnvironmentMapBuild m_build;
        Bool m_built = false;
    };

    void EnvironmentLibrary::Initialize(Renderer* renderer, const AssetSystem* assets, TaskManager* tasks)
    {
        m_renderer = renderer;
        m_assets = assets;
        m_tasks = tasks;
    }

    void EnvironmentLibrary::Shutdown()
    {
        CancelBakes();
        // 태스크 관리자가 먼저 내려갔으면(호스트의 끄는 순서) 남은 콜백이 이미 모두 불려 이 수가 0 이다.
        while (m_bakesInFlight > 0 && m_tasks != nullptr && m_tasks->IsInitialized())
        {
            const UInt32 before = m_bakesInFlight;
            m_tasks->Update();
            if (m_bakesInFlight == before)
            {
                std::this_thread::yield();
            }
        }
        Release();
        m_baked = EnvironmentMapBuild{};
        m_bakedReady = false;
        m_failed = {};
        m_failedGeneration = 0;
        m_renderer = nullptr;
        m_assets = nullptr;
        m_tasks = nullptr;
    }

    void EnvironmentLibrary::CancelBakes()
    {
        if (m_wantedActive && m_wantedGroup != 0 && m_tasks != nullptr && m_tasks->IsInitialized())
        {
            if (TaskGroup* group = m_tasks->FindGroup(m_wantedGroup))
            {
                group->RequestCancel();
            }
        }
        m_wanted = {};
        m_wantedGeneration = 0;
        m_wantedActive = false;
        m_wantedGroup = 0;
        m_bakedReady = false;
        m_baked = EnvironmentMapBuild{};
    }

    void EnvironmentLibrary::StartBake(AssetHandle sky, const EnvironmentMapData& data)
    {
        CancelBakes();
        m_wanted = sky;
        m_wantedGeneration = data.dataGeneration;
        m_wantedActive = true;
        const UInt32 serial = ++m_bakeSerial;
        const std::size_t count = static_cast<std::size_t>(data.width.Get()) * data.height.Get() * 3u;
        if (data.width == 0 || data.height == 0 || data.rgb.Size() < count)
        {
            FinishBake(serial, false, true, false, EnvironmentMapBuild{});
            return;
        }
        if (m_tasks == nullptr || false == m_tasks->IsInitialized())
        {
            EnvironmentMapBuild build;
            const Bool built = BuildEnvironmentMap(data.rgb.Data(), data.width, data.height, build);
            FinishBake(serial, false, true, built, std::move(build));
            return;
        }
        // 원본을 뜬다. 큰 하늘도 복사 한 번이고(4096 x 2048 이 100 MB), 굽기 전체에 비하면 짧다.
        Array<Float> rgb;
        rgb.Append(data.rgb.Data(), count);
        OwnerPtr<TaskGroup> group = MakeOwnerPtr<TaskGroup>(String("Bake a sky map"));
        group->Add(MakeOwnerPtr<EnvironmentBakeTask>(*this, serial, data.width, data.height, std::move(rgb)));
        const TaskGroupId id = m_tasks->Submit(std::move(group));
        if (id == InvalidTaskGroupId)
        {
            EnvironmentMapBuild build;
            const Bool built = BuildEnvironmentMap(data.rgb.Data(), data.width, data.height, build);
            FinishBake(serial, false, true, built, std::move(build));
            return;
        }
        m_wantedGroup = id;
        ++m_bakesInFlight;
    }

    void EnvironmentLibrary::FinishBake(UInt32 serial, Bool fromWorker, Bool completed, Bool built, EnvironmentMapBuild&& build)
    {
        if (fromWorker && m_bakesInFlight > 0)
        {
            --m_bakesInFlight;
        }
        // 멈추라고 한 굽기(다른 하늘로 바뀌었거나 내려가는 중)의 결과는 버린다. 같은 하늘로 돌아와 새로 굽는 중이어도 번호가 다르다.
        if (false == m_wantedActive || serial != m_bakeSerial || false == completed)
        {
            return;
        }
        m_wantedGroup = 0;
        m_bakedReady = true;
        m_bakedOk = built;
        m_baked = std::move(build);
    }

    void EnvironmentLibrary::Release()
    {
        if (m_renderer != nullptr && m_uploaded.generation != 0)
        {
            m_renderer->UnregisterTexture(m_uploaded);
        }
        m_uploaded = {};
        m_source = {};
        m_sourceGeneration = 0;
        for (auto& coefficient : m_sh)
        {
            coefficient[0] = 0.0f;
            coefficient[1] = 0.0f;
            coefficient[2] = 0.0f;
        }
    }

    void EnvironmentLibrary::Resolve(const CanvasEnvironment& environment, Environment3D& result)
    {
        result = Environment3D{};
        result.ambient = static_cast<EnvironmentAmbient>(static_cast<std::uint8_t>(environment.ambientSource));
        const Float ambient = environment.ambientIntensity < 0.0f ? Float(0.0f) : environment.ambientIntensity;
        const Color* colors[4] = {&environment.ambientColor, &environment.skyColor, &environment.equatorColor, &environment.groundColor};
        Float* targets[4] = {result.ambientColor, result.skyColor, result.equatorColor, result.groundColor};
        for (Int32 at = 0; at < 4; ++at)
        {
            targets[at.Get()][0] = colors[at.Get()]->R * ambient;
            targets[at.Get()][1] = colors[at.Get()]->G * ambient;
            targets[at.Get()][2] = colors[at.Get()]->B * ambient;
        }
        const Float sky = environment.skyIntensity < 0.0f ? Float(0.0f) : environment.skyIntensity;
        result.skyIntensity = sky;
        result.skyRotation = environment.skyRotation * (3.14159265f / 180.0f);
        result.reflectionIntensity = environment.reflectionIntensity < 0.0f ? Float(0.0f) : environment.reflectionIntensity;

        // 하늘 맵. 하늘이 바뀌었거나 같은 하늘이 다시 읽혔으면(자료 세대) 워커에서 다시 굽고, 다 구운 것은 여기서 올린다(D-449).
        const EnvironmentMapData* data = m_assets != nullptr && environment.sky.generation != 0 ? m_assets->GetEnvironmentMap(environment.sky) : nullptr;
        if (data == nullptr || m_renderer == nullptr)
        {
            // 하늘을 비웠다. 굽던 것을 멈추고 올린 것을 바로 내린다.
            if (m_wantedActive)
            {
                CancelBakes();
            }
            if (m_uploaded.generation != 0)
            {
                Release();
            }
            return;
        }
        const auto matches = [&](AssetHandle sky, UInt32 generation) -> Bool {
            return sky.index == environment.sky.index && sky.generation == environment.sky.generation && generation == data->dataGeneration;
        };
        const Bool uploaded = m_uploaded.generation != 0 && matches(m_source, m_sourceGeneration);
        const Bool failed = m_failed.generation != 0 && matches(m_failed, m_failedGeneration);
        if (false == uploaded && false == failed && false == (m_wantedActive && matches(m_wanted, m_wantedGeneration)))
        {
            StartBake(environment.sky, *data);
        }
        if (m_bakedReady && m_wantedActive && matches(m_wanted, m_wantedGeneration))
        {
            // 다 구웠다. 앞 하늘을 내리고 올린다. 굽지 못했으면 이 하늘·이 세대는 다시 굽지 않는다(파일이 바뀌면 세대가 오른다).
            Release();
            if (m_bakedOk)
            {
                m_uploaded = m_renderer->RegisterEnvironmentMap(m_baked);
                ++m_buildCount;
            }
            if (m_uploaded.generation != 0)
            {
                m_source = m_wanted;
                m_sourceGeneration = m_wantedGeneration;
                for (Int32 at = 0; at < 9; ++at)
                {
                    for (Int32 channel = 0; channel < 3; ++channel)
                    {
                        m_sh[at.Get()][channel.Get()] = m_baked.sh[at.Get()][channel.Get()];
                    }
                }
                m_failed = {};
                m_failedGeneration = 0;
            }
            else
            {
                Log::Write(LogLevel::Warning, "environment", "a sky map could not be baked or uploaded; the canvas draws without it");
                m_failed = m_wanted;
                m_failedGeneration = m_wantedGeneration;
            }
            m_wanted = {};
            m_wantedGeneration = 0;
            m_wantedActive = false;
            m_bakedReady = false;
            m_baked = EnvironmentMapBuild{};
        }
        // 굽는 동안은 앞 하늘을 그대로 그린다(처음이면 하늘 없이).
        if (m_uploaded.generation == 0)
        {
            return;
        }
        result.map = m_uploaded;        // 확산광은 하늘의 세기와 환경광 세기를 함께 받는다(배경과 반사는 하늘의 세기만).
        for (Int32 at = 0; at < 9; ++at)
        {
            for (Int32 channel = 0; channel < 3; ++channel)
            {
                result.sh[at.Get()][channel.Get()] = m_sh[at.Get()][channel.Get()] * sky * ambient;
            }
        }
    }
}
