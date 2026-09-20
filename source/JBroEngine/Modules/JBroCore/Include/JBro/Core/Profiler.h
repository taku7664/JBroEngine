#pragma once

#include <JBro/Core/Core.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    // 프레임 안에서 어디에 시간이 갔는지 잰다(D-138). 기존 엔진 `Engine.CpuProfiler` 자리다.
    //
    // **왜 필요한가**: 통계 창은 프레임 시간 하나만 말했다. 느려졌을 때 어디가 느린지는
    // 그 숫자로 알 수 없고, 기존 엔진에는 구간별로 나눠 보여 주는 창이 있었다.
    //
    // **매 프레임 도는 경로다.** 힙 할당도, 문자열 생성·비교도 하지 않는다(§7).
    // 이름은 **리터럴만** 받아 포인터로 들고, 같은 이름은 같은 주소라는 것에 기댄다 -
    // 그래서 같은 구간이 한 프레임에 여러 번 돌아도 한 줄로 합쳐진다.
    //
    // **메인 스레드 전용이다.** 워커에서 부르지 않는다.
    struct ProfileSample
    {
        // 리터럴을 그대로 든다. 복사하지 않으므로 살아 있는 글자여야 한다.
        const char* name = nullptr;
        // 몇 겹 안인가. 0 이 가장 바깥이다.
        std::uint32_t depth = 0;
        // 이 프레임에 이 구간이 돈 횟수와 그 합.
        std::uint32_t callCount = 0;
        std::uint64_t totalNanoseconds = 0;
    };

    namespace Profiler
    {
        // 한 프레임에 담는 구간 수와 겹의 한계다. 넘치면 그 구간은 세지 않는다 -
        // 재려다 프레임을 늘리지 않는다.
        inline constexpr std::size_t MaxSamples = 128;
        inline constexpr std::size_t MaxDepth = 16;

        // **꺼져 있으면 아무것도 하지 않는다.** 기본은 꺼짐이다 - 게임 실행이 재는 값이 아니다.
        void SetEnabled(bool enabled);
        bool IsEnabled();

        // 프레임의 처음과 끝. `EndFrame` 이 이번 프레임의 결과를 읽을 수 있는 자리로 옮긴다.
        void BeginFrame();
        void EndFrame();

        void Push(const char* literalName);
        void Pop();

        // **지난 프레임의 결과다.** 쌓는 것과 읽는 것을 갈라야 창이 반쯤 찬 목록을 그리지 않는다.
        std::size_t GetCount();
        const ProfileSample* GetAt(std::size_t index);
        // 프레임 전체의 시간. 구간의 합과 견주면 재지 못한 몫이 보인다.
        std::uint64_t GetFrameNanoseconds();
    }

    // 스스로 닫는 구간이다. 중간에 돌아 나가는 길이 생겨도 짝이 어긋나지 않는다.
    class ProfileScope
    {
    public:
        explicit ProfileScope(const char* literalName)
        {
            Profiler::Push(literalName);
        }
        ~ProfileScope()
        {
            Profiler::Pop();
        }
        ProfileScope(const ProfileScope&) = delete;
        ProfileScope& operator=(const ProfileScope&) = delete;
    };
}

// 이름은 **리터럴이어야 한다**. 변수를 넘기면 그 주소로 묶이므로 줄마다 다른 칸이 된다.
#define JBRO_PROFILE_SCOPE_JOIN2(a, b) a##b
#define JBRO_PROFILE_SCOPE_JOIN(a, b) JBRO_PROFILE_SCOPE_JOIN2(a, b)
#define JBRO_PROFILE_SCOPE(literalName) \
    const ::JBro::ProfileScope JBRO_PROFILE_SCOPE_JOIN(jbroProfileScope_, __LINE__)(literalName)
