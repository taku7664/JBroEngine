#include <JBro/Core/Log.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace
{
    void Check(JBro::Bool condition, const char* message)
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
        JBro::Bool m_previous = true;
    };

    // 쌓은 줄을 그대로, **오래된 것부터** 돌려주어야 한다. 로그 창이 위에서 아래로 읽는 순서다.
    void TestEntriesComeBackOldestFirst()
    {
        const QuietLog quiet;
        JBro::Log::Clear();
        const JBro::UInt64 before = JBro::Log::GetRevision();

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
        char expectedNewest[32] = {};
        std::snprintf(expectedNewest, sizeof(expectedNewest), "%zu", JBro::Log::Capacity + Extra - 1);
        Check(newest != nullptr && std::strcmp(newest->message, expectedNewest) == 0,
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
        const JBro::UInt64 before = JBro::Log::GetRevision();
        JBro::Log::Clear();
        Check(JBro::Log::GetCount() == 0, "clearing empties the ring");
        Check(JBro::Log::GetRevision() > before, "and still moves the revision forward");
    }

    void TestExplicitKeysGroupDifferentMessagesAndKeepOccurrenceTimes()
    {
        const QuietLog quiet;
        JBro::Log::Clear();
        const JBro::UInt64 epoch = JBro::Log::GetClearEpoch();
        const JBro::UInt64 session = JBro::Log::GetSessionId();
        JBro::Log::WriteGrouped(JBro::LogLevel::Error, "script", "site:12:target2", "first object");
        JBro::Log::Write(JBro::LogLevel::Error, "script", "unkeyed");
        JBro::Log::WriteGrouped(JBro::LogLevel::Error, "script", "site:12:target2", "second object");
        Check(JBro::Log::GetGroupCount() == 1, "only explicit equal keys group");
        const JBro::LogGroup* group = JBro::Log::GetGroupAt(0);
        Check(group != nullptr && group->count == 2, "group total includes both occurrences");
        Check(std::strcmp(group->key.c_str(), "site:12:target2") == 0, "group keeps writer key");
        Check(group->latestSerial == JBro::Log::GetAt(2)->serial, "group points at latest event");
        Check(std::strstr(group->display, "second object (2)") != nullptr,
            "the collapsed row shows the latest message and total");
        Check(group->latestTimestampMs >= group->firstTimestampMs, "group time advances");
        Check(JBro::Log::GetAt(0)->groupId == JBro::Log::GetAt(2)->groupId,
            "same key uses the same group id even with a different message");
        Check(JBro::Log::GetAt(1)->groupId == 0, "plain write stays ungrouped");
        Check(JBro::Log::GetAt(0)->timestampMs > 0 && JBro::Log::GetAt(2)->timestampMs > 0,
            "each occurrence carries its own time");
        Check(JBro::Log::GetSessionId() == session && JBro::Log::GetClearEpoch() == epoch,
            "writing does not replace the session or clear generation");
    }

    void TestGroupTotalsSurviveOccurrenceEvictionAndClearResetsThem()
    {
        const QuietLog quiet;
        JBro::Log::Clear();
        for (std::size_t index = 0; index < JBro::Log::Capacity + 3; ++index)
        {
            JBro::Log::WriteGrouped(JBro::LogLevel::Warning, "script", "same-site", "hit %zu", index);
        }
        const JBro::LogGroup* group = JBro::Log::GetGroupAt(0);
        Check(group != nullptr && group->count == JBro::Log::Capacity + 3,
            "eviction must not change exact session total");
        Check(JBro::Log::GetAt(0)->groupId == group->id, "retained occurrence still identifies the group");
        Check(JBro::Log::GetAt(0)->serial > group->firstSerial,
            "client can detect omitted early occurrences");
        const JBro::UInt64 beforeEpoch = JBro::Log::GetClearEpoch();
        JBro::Log::Clear();
        Check(JBro::Log::GetGroupCount() == 0 && JBro::Log::GetCount() == 0,
            "clear removes summary and details together");
        Check(JBro::Log::GetClearEpoch() == beforeEpoch + 1,
            "clear generation changes for the remote editor");
    }

    void TestMatchingTextDoesNotGroupWithoutTheSameWriterKey()
    {
        const QuietLog quiet;
        JBro::Log::Clear();
        JBro::Log::WriteGrouped(JBro::LogLevel::Error, "script", "site-a", "same message");
        JBro::Log::WriteGrouped(JBro::LogLevel::Error, "script", "site-b", "same message");
        JBro::Log::Write(JBro::LogLevel::Error, "script", "same message");
        JBro::Log::Write(JBro::LogLevel::Error, "script", "same message");
        Check(JBro::Log::GetGroupCount() == 2, "different explicit keys remain different groups");
        Check(JBro::Log::GetAt(0)->groupId != JBro::Log::GetAt(1)->groupId,
            "identical text never overrides the writer key");
        Check(JBro::Log::GetAt(2)->groupId == 0 && JBro::Log::GetAt(3)->groupId == 0,
            "even repeated unkeyed text stays as two rows");
    }
}

JBro::Int32 RunLogTests()
{
    TestEntriesComeBackOldestFirst();
    TestTheRingDropsTheOldest();
    TestALongLineSaysItWasCut();
    TestClearingMovesTheRevisionForward();
    TestExplicitKeysGroupDifferentMessagesAndKeepOccurrenceTimes();
    TestGroupTotalsSurviveOccurrenceEvictionAndClearResetsThem();
    TestMatchingTextDoesNotGroupWithoutTheSameWriterKey();
    // 검사가 남긴 줄은 치운다. 뒤따르는 테스트의 로그 창이 이것을 보지 않게.
    JBro::Log::Clear();
    std::cout << "Log tests passed.\n";
    return 0;
}
