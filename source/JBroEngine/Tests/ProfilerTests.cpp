#include <JBro/Core/Profiler.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    const JBro::ProfileSample* Find(const char* name, std::uint32_t depth)
    {
        for (std::size_t index = 0; index < JBro::Profiler::GetCount(); ++index)
        {
            const JBro::ProfileSample* sample = JBro::Profiler::GetAt(index);
            if (sample != nullptr && sample->depth == depth
                && std::strcmp(sample->name, name) == 0)
            {
                return sample;
            }
        }
        return nullptr;
    }

    // **꺼져 있으면 아무것도 쌓지 않는다.** 게임 실행이 재는 값이 아니다.
    void TestNothingIsMeasuredWhileItIsOff()
    {
        JBro::Profiler::SetEnabled(false);
        JBro::Profiler::BeginFrame();
        {
            JBRO_PROFILE_SCOPE("Off");
        }
        JBro::Profiler::EndFrame();
        Check(JBro::Profiler::GetCount() == 0, "a frame measured while off must be empty");
    }

    // 겹은 그대로 남고, **같은 이름의 같은 겹은 한 줄로 합쳐진다**.
    void TestNestedScopesKeepTheirDepthAndMerge()
    {
        JBro::Profiler::SetEnabled(true);
        JBro::Profiler::BeginFrame();
        {
            JBRO_PROFILE_SCOPE("Frame");
            {
                JBRO_PROFILE_SCOPE("Inner");
            }
            {
                // 같은 이름·같은 겹이다. 한 줄에 두 번으로 세야 한다.
                JBRO_PROFILE_SCOPE("Inner");
            }
            {
                JBRO_PROFILE_SCOPE("Other");
            }
        }
        JBro::Profiler::EndFrame();

        const JBro::ProfileSample* frame = Find("Frame", 0);
        Check(frame != nullptr, "the outer scope must be there");
        Check(frame->callCount == 1, "once");

        const JBro::ProfileSample* inner = Find("Inner", 1);
        Check(inner != nullptr, "and the inner one, one level in");
        Check(inner->callCount == 2, "counted twice, not listed twice");
        Check(Find("Other", 1) != nullptr, "with its neighbour beside it");
        Check(Find("Inner", 0) == nullptr, "the same name at another depth is another row");

        // 프레임 전체는 구간의 합보다 작지 않다.
        Check(JBro::Profiler::GetFrameNanoseconds() >= frame->totalNanoseconds,
            "the frame cannot be shorter than what it contains");
        JBro::Profiler::SetEnabled(false);
    }

    // 다음 프레임은 **지난 프레임을 덮지 않는다.** 창은 다 쌓인 것을 읽어야 한다.
    void TestTheLastFrameStaysReadableWhileTheNextIsBuilt()
    {
        JBro::Profiler::SetEnabled(true);
        JBro::Profiler::BeginFrame();
        {
            JBRO_PROFILE_SCOPE("First");
        }
        JBro::Profiler::EndFrame();
        Check(Find("First", 0) != nullptr, "the first frame is readable");

        JBro::Profiler::BeginFrame();
        {
            JBRO_PROFILE_SCOPE("Second");
        }
        // 아직 닫지 않았다. 읽히는 것은 여전히 지난 프레임이어야 한다.
        Check(Find("First", 0) != nullptr, "and stays readable while the next is built");
        Check(Find("Second", 0) == nullptr, "the one being built is not readable yet");

        JBro::Profiler::EndFrame();
        Check(Find("Second", 0) != nullptr, "closing the frame makes it readable");
        Check(Find("First", 0) == nullptr, "and the one before is gone");
        JBro::Profiler::SetEnabled(false);
    }

    // 겹이 한계를 넘어도 **짝은 맞는다.** 어긋나면 그 뒤의 모든 구간이 엉뚱한 겹에 들어간다.
    void TestOverflowingTheDepthDoesNotUnbalance()
    {
        JBro::Profiler::SetEnabled(true);
        JBro::Profiler::BeginFrame();
        {
            JBRO_PROFILE_SCOPE("Root");
            for (std::size_t index = 0; index < JBro::Profiler::MaxDepth + 4; ++index)
            {
                JBro::Profiler::Push("Deep");
            }
            for (std::size_t index = 0; index < JBro::Profiler::MaxDepth + 4; ++index)
            {
                JBro::Profiler::Pop();
            }
            {
                // 다시 한 겹이어야 한다. 짝이 어긋났으면 여기가 다른 겹으로 간다.
                JBRO_PROFILE_SCOPE("AfterOverflow");
            }
        }
        JBro::Profiler::EndFrame();
        Check(Find("Root", 0) != nullptr, "the root is still the root");
        Check(Find("AfterOverflow", 1) != nullptr,
            "and what comes after the overflow is back at the depth it should be");
        JBro::Profiler::SetEnabled(false);
    }
}

int RunProfilerTests()
{
    TestNothingIsMeasuredWhileItIsOff();
    TestNestedScopesKeepTheirDepthAndMerge();
    TestTheLastFrameStaysReadableWhileTheNextIsBuilt();
    TestOverflowingTheDepthDoesNotUnbalance();
    std::cout << "Profiler tests passed.\n";
    return 0;
}
