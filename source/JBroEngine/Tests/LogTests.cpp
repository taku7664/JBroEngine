#include <JBro/Core/Log.h>

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

    // 로그가 콘솔로도 나가면 테스트 출력이 뒤섞인다. 검사하는 동안만 끈다.
    class QuietLog
    {
    public:
        QuietLog()
            : m_previous(JBro::Log::GetEchoToConsole())
        {
            JBro::Log::SetEchoToConsole(false);
        }
        ~QuietLog()
        {
            JBro::Log::SetEchoToConsole(m_previous);
        }
        QuietLog(const QuietLog&) = delete;
        QuietLog& operator=(const QuietLog&) = delete;

    private:
        bool m_previous = true;
    };

    // 쌓은 줄을 그대로, **오래된 것부터** 돌려주어야 한다. 로그 창이 위에서 아래로 읽는 순서다.
    void TestEntriesComeBackOldestFirst()
    {
        const QuietLog quiet;
        JBro::Log::Clear();
        const std::uint64_t before = JBro::Log::GetRevision();

        JBro::Log::Write(JBro::LogLevel::Info, "probe", "first %d", 1);
        JBro::Log::Write(JBro::LogLevel::Warning, "probe", "second");

        Check(JBro::Log::GetCount() == 2, "both lines must be kept");
        Check(JBro::Log::GetRevision() > before, "and the revision must move, or nobody re-reads");

        const JBro::LogEntry* first = JBro::Log::GetAt(0);
        const JBro::LogEntry* second = JBro::Log::GetAt(1);
        Check(first != nullptr && second != nullptr, "both must be reachable");
        Check(std::strcmp(first->message, "first 1") == 0, "the oldest comes first, formatted");
        Check(first->level == JBro::LogLevel::Info, "with the level it was written at");
        Check(std::strcmp(first->category, "probe") == 0, "and its category");
        Check(std::strcmp(second->message, "second") == 0, "then the next one");
        Check(second->level == JBro::LogLevel::Warning, "with its own level");
        Check(JBro::Log::GetAt(2) == nullptr, "and there is no third");
    }

    // **가득 차면 오래된 것부터 밀린다.** 끝없이 쌓으면 오래 띄워 둔 에디터가 메모리를 먹는다.
    void TestTheRingDropsTheOldest()
    {
        const QuietLog quiet;
        JBro::Log::Clear();
        constexpr std::size_t Extra = 5;
        for (std::size_t index = 0; index < JBro::Log::Capacity + Extra; ++index)
        {
            JBro::Log::Write(JBro::LogLevel::Debug, "ring", "%zu", index);
        }
        Check(JBro::Log::GetCount() == JBro::Log::Capacity,
            "the ring never grows past its capacity");

        const JBro::LogEntry* oldest = JBro::Log::GetAt(0);
        Check(oldest != nullptr, "the oldest must still be reachable");
        Check(std::strcmp(oldest->message, "5") == 0,
            "and it must be the first one that was not pushed out");
        const JBro::LogEntry* newest = JBro::Log::GetAt(JBro::Log::Capacity - 1);
        Check(newest != nullptr && std::strcmp(newest->message, "516") == 0,
            "with the newest at the end");
        // 일련번호는 고리가 돌아도 늘어난다. 줄을 Id 로 쓰는 쪽이 이것에 기댄다.
        Check(newest->serial > oldest->serial, "serials keep counting past the wrap");
    }

    // 긴 줄은 잘리되, **잘렸다는 것이 보여야** 한다.
    void TestALongLineSaysItWasCut()
    {
        const QuietLog quiet;
        JBro::Log::Clear();
        char long_[JBro::LogEntry::MaxMessage * 2];
        std::memset(long_, 'x', sizeof(long_) - 1);
        long_[sizeof(long_) - 1] = '\0';
        JBro::Log::Write(JBro::LogLevel::Error, "cut", "%s", long_);

        const JBro::LogEntry* entry = JBro::Log::GetAt(0);
        Check(entry != nullptr, "the line must be kept");
        Check(std::strlen(entry->message) == JBro::LogEntry::MaxMessage - 1,
            "cut to what fits");
        Check(std::strcmp(entry->message + JBro::LogEntry::MaxMessage - 4, "...") == 0,
            "and ending in an ellipsis, so a cut line is not read as the whole thing");
    }

    // 지우면 비되, **판번호는 되돌리지 않는다.** 보던 쪽이 "달라졌다" 로 읽어야 다시 읽는다.
    void TestClearingMovesTheRevisionForward()
    {
        const QuietLog quiet;
        JBro::Log::Write(JBro::LogLevel::Info, "clear", "something");
        const std::uint64_t before = JBro::Log::GetRevision();
        JBro::Log::Clear();
        Check(JBro::Log::GetCount() == 0, "clearing empties the ring");
        Check(JBro::Log::GetRevision() > before, "and still moves the revision forward");
    }
}

int RunLogTests()
{
    TestEntriesComeBackOldestFirst();
    TestTheRingDropsTheOldest();
    TestALongLineSaysItWasCut();
    TestClearingMovesTheRevisionForward();
    // 검사가 남긴 줄은 치운다. 뒤따르는 테스트의 로그 창이 이것을 보지 않게.
    JBro::Log::Clear();
    std::cout << "Log tests passed.\n";
    return 0;
}
