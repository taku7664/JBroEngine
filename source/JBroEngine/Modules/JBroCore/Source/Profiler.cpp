#include <JBro/Core/Profiler.h>

#include <chrono>

namespace JBro::Profiler
{
    namespace
    {
        std::uint64_t NowNanoseconds()
        {
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
        }

        struct Open
        {
            std::size_t sample = 0;
            std::uint64_t startedAt = 0;
        };

        struct State
        {
            // 쌓는 자리와 읽는 자리를 나눈다. 창이 반쯤 찬 목록을 그리지 않는다.
            ProfileSample building[MaxSamples];
            std::size_t buildingCount = 0;
            ProfileSample finished[MaxSamples];
            std::size_t finishedCount = 0;

            Open stack[MaxDepth];
            std::size_t depth = 0;

            std::uint64_t frameStartedAt = 0;
            std::uint64_t frameNanoseconds = 0;
            bool enabled = false;
            bool inFrame = false;
        };

        State& Get()
        {
            static State state;
            return state;
        }
    }

    void SetEnabled(bool enabled)
    {
        State& state = Get();
        if (state.enabled == enabled)
        {
            return;
        }
        state.enabled = enabled;
        if (false == enabled)
        {
            // 끄는 순간의 반쯤 쌓인 프레임은 버린다. 남겨 두면 다시 켰을 때
            // 지난 세기의 숫자가 한 프레임 섞여 보인다.
            state.buildingCount = 0;
            state.finishedCount = 0;
            state.depth = 0;
            state.inFrame = false;
            state.frameNanoseconds = 0;
        }
    }

    bool IsEnabled()
    {
        return Get().enabled;
    }

    void BeginFrame()
    {
        State& state = Get();
        if (false == state.enabled)
        {
            return;
        }
        state.buildingCount = 0;
        state.depth = 0;
        state.frameStartedAt = NowNanoseconds();
        state.inFrame = true;
    }

    void EndFrame()
    {
        State& state = Get();
        if (false == state.inFrame)
        {
            return;
        }
        state.inFrame = false;
        state.frameNanoseconds = NowNanoseconds() - state.frameStartedAt;
        // 닫히지 않은 구간이 있으면 그 프레임의 겹은 맞지 않는다. 그래도 가진 것은 보인다 -
        // 버리면 왜 안 나오는지 화면에서 알 수 없다.
        state.depth = 0;
        for (std::size_t index = 0; index < state.buildingCount; ++index)
        {
            state.finished[index] = state.building[index];
        }
        state.finishedCount = state.buildingCount;
    }

    void Push(const char* literalName)
    {
        State& state = Get();
        if (false == state.inFrame || literalName == nullptr || state.depth >= MaxDepth)
        {
            // **겹이 넘치면 세지 않는다.** 그래도 `Pop` 은 짝을 맞춰야 하므로 겹만 올린다.
            if (state.inFrame)
            {
                ++state.depth;
            }
            return;
        }

        const std::uint32_t depth = static_cast<std::uint32_t>(state.depth);
        // 같은 이름·같은 겹이면 한 줄로 합친다. 이름은 리터럴이라 주소 비교로 끝난다.
        std::size_t found = state.buildingCount;
        for (std::size_t index = 0; index < state.buildingCount; ++index)
        {
            if (state.building[index].name == literalName
                && state.building[index].depth == depth)
            {
                found = index;
                break;
            }
        }
        if (found == state.buildingCount)
        {
            if (state.buildingCount >= MaxSamples)
            {
                // 칸이 없다. 재려다 프레임을 늘리지 않는다.
                ++state.depth;
                return;
            }
            ProfileSample& sample = state.building[state.buildingCount];
            sample.name = literalName;
            sample.depth = depth;
            sample.callCount = 0;
            sample.totalNanoseconds = 0;
            ++state.buildingCount;
        }

        Open& open = state.stack[state.depth];
        open.sample = found;
        open.startedAt = NowNanoseconds();
        ++state.depth;
    }

    void Pop()
    {
        State& state = Get();
        if (false == state.inFrame || state.depth == 0)
        {
            return;
        }
        --state.depth;
        if (state.depth >= MaxDepth)
        {
            // 넘쳐서 세지 않은 구간이다.
            return;
        }
        const Open& open = state.stack[state.depth];
        if (open.startedAt == 0 || open.sample >= state.buildingCount)
        {
            return;
        }
        ProfileSample& sample = state.building[open.sample];
        sample.totalNanoseconds += NowNanoseconds() - open.startedAt;
        ++sample.callCount;
    }

    std::size_t GetCount()
    {
        return Get().finishedCount;
    }

    const ProfileSample* GetAt(std::size_t index)
    {
        const State& state = Get();
        if (index >= state.finishedCount)
        {
            return nullptr;
        }
        return &state.finished[index];
    }

    std::uint64_t GetFrameNanoseconds()
    {
        return Get().frameNanoseconds;
    }
}
