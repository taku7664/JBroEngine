#include <JBro/Core/Log.h>

#include <cstdio>
#include <cstring>
#include <chrono>
#include <ctime>
#include <map>
#include <string>
#include <vector>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::Log
{
    namespace
    {
        struct Ring
        {
            LogEntry entries[Capacity];
            // 다음에 쓸 자리. 한 바퀴 돌면 가장 오래된 줄을 덮는다.
            std::size_t next = 0;
            std::size_t count = 0;
            UInt64 revision = 0;
            UInt64 serial = 0;
            UInt64 clearEpoch = 0;
            UInt64 sessionId = 0;
            std::map<std::string, std::size_t, std::less<>> groupIndex;
            std::vector<LogGroup> groups;
            Bool echo = true;
        };

        Ring& Get()
        {
            // 프로세스에 하나다. 엔진 인스턴스가 여럿이어도 사람이 보는 로그는 한 줄기다.
            static Ring ring;
            if (ring.sessionId == 0)
            {
                const auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
                const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
                ring.sessionId = static_cast<std::uint64_t>(ticks)
                    ^ static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&ring));
                if (ring.sessionId == 0)
                {
                    ring.sessionId = 1;
                }
            }
            return ring;
        }

        UInt64 NowMilliseconds()
        {
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
        }

        void FormatLocalTime(UInt64 milliseconds, char (&output)[24])
        {
            const std::time_t seconds = static_cast<std::time_t>(milliseconds.Get() / 1000);
            std::tm local = {};
#if defined(_WIN32)
            const Bool valid = localtime_s(&local, &seconds) == 0;
#else
            const Bool valid = localtime_r(&seconds, &local) != nullptr;
#endif
            if (false == valid || std::strftime(output, sizeof(output), "%Y-%m-%d %H:%M:%S", &local) != 19)
            {
                output[0] = '\0';
                return;
            }
            std::snprintf(output + 19, sizeof(output) - 19, ".%03u",
                static_cast<unsigned>(milliseconds.Get() % 1000));
        }

        void CopyBounded(char* destination, std::size_t capacity, const char* source)
        {
            if (capacity == 0)
            {
                return;
            }
            if (source == nullptr)
            {
                destination[0] = '\0';
                return;
            }
            const std::size_t length = std::strlen(source);
            const std::size_t room = capacity - 1;
            const std::size_t taken = length < room ? length : room;
            std::memcpy(destination, source, taken);
            destination[taken] = '\0';
        }

        const char* LevelTag(LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Trace: return "trace";
            case LogLevel::Debug: return "debug";
            case LogLevel::Info: return "note";
            case LogLevel::Warning: return "warning";
            case LogLevel::Error: return "error";
            default: return "note";
            }
        }
    }

    void WriteGroupedV(LogLevel level, const char* category, const char* key, const char* format, std::va_list args)
    {
        Ring& ring = Get();
        LogEntry& entry = ring.entries[ring.next];
        entry.level = level;
        entry.serial = ++ring.serial;
        entry.timestampMs = NowMilliseconds();
        FormatLocalTime(entry.timestampMs, entry.timeText);
        entry.groupId = 0;
        if (key != nullptr && key[0] != '\0')
        {
            auto found = ring.groupIndex.find(key);
            std::size_t index = 0;
            if (found == ring.groupIndex.end())
            {
                index = ring.groups.size();
                LogGroup group;
                group.id = static_cast<std::uint64_t>(index + 1);
                group.key = key;
                group.firstSerial = entry.serial;
                group.firstTimestampMs = entry.timestampMs;
                ring.groups.push_back(std::move(group));
                ring.groupIndex.emplace(key, index);
            }
            else
            {
                index = found->second;
            }
            LogGroup& group = ring.groups[index];
            ++group.count;
            group.latestSerial = entry.serial;
            group.latestTimestampMs = entry.timestampMs;
            entry.groupId = group.id;
        }
        CopyBounded(entry.category, LogEntry::MaxCategory, category);

        if (format == nullptr)
        {
            entry.message[0] = '\0';
        }
        else
        {
            // `vsnprintf` 는 잘려도 늘 널로 끝낸다. 잘린 것은 그 자리에서 알린다 -
            // 조용히 자르면 뒷말이 없어진 줄을 원문으로 읽게 된다.
            const Int32 written = std::vsnprintf(
                entry.message, LogEntry::MaxMessage, format, args);
            if (written < 0)
            {
                entry.message[0] = '\0';
            }
            else if (static_cast<std::size_t>(written) >= LogEntry::MaxMessage)
            {
                std::memcpy(entry.message + LogEntry::MaxMessage - 4, "...", 4);
            }
        }

        std::snprintf(entry.display, sizeof(entry.display), "%s [%s] %s",
            entry.timeText, entry.category, entry.message);
        if (entry.groupId != 0)
        {
            LogGroup& group = ring.groups[static_cast<std::size_t>(entry.groupId.Get() - 1)];
            group.latestLevel = level;
            CopyBounded(group.latestTimeText, sizeof(group.latestTimeText), entry.timeText);
            std::snprintf(group.display, sizeof(group.display), "%s [%s] %s (%llu)###log_group",
                entry.timeText, entry.category, entry.message,
                static_cast<unsigned long long>(group.count.Get()));
        }

        ring.next = (ring.next + 1) % Capacity;
        if (ring.count < Capacity)
        {
            ++ring.count;
        }
        ++ring.revision;

        if (ring.echo)
        {
            if (entry.category[0] != '\0')
            {
                std::printf("%s: %s: %s\n", LevelTag(level), entry.category, entry.message);
            }
            else
            {
                std::printf("%s: %s\n", LevelTag(level), entry.message);
            }
        }
    }

    void WriteV(LogLevel level, const char* category, const char* format, std::va_list args)
    {
        WriteGroupedV(level, category, nullptr, format, args);
    }

    void WriteFormatted(LogLevel level, const char* category, const char* format, ...)
    {
        std::va_list args;
        va_start(args, format);
        WriteV(level, category, format, args);
        va_end(args);
    }

    void WriteGroupedFormatted(LogLevel level, const char* category, const char* key, const char* format, ...)
    {
        std::va_list args;
        va_start(args, format);
        WriteGroupedV(level, category, key, format, args);
        va_end(args);
    }

    UInt64 GetRevision()
    {
        return Get().revision;
    }

    std::size_t GetCount()
    {
        return Get().count;
    }

    const LogEntry* GetAt(std::size_t index)
    {
        const Ring& ring = Get();
        if (index >= ring.count)
        {
            return nullptr;
        }
        // 0 이 가장 오래된 줄이다. 아직 한 바퀴 돌지 않았으면 쓴 순서 그대로다.
        const std::size_t oldest = ring.count < Capacity
            ? 0
            : ring.next;
        return &ring.entries[(oldest + index) % Capacity];
    }

    std::size_t GetGroupCount()
    {
        return Get().groups.size();
    }

    const LogGroup* GetGroupAt(std::size_t index)
    {
        const Ring& ring = Get();
        return index < ring.groups.size() ? &ring.groups[index] : nullptr;
    }

    UInt64 GetSessionId()
    {
        return Get().sessionId;
    }

    UInt64 GetClearEpoch()
    {
        return Get().clearEpoch;
    }

    void Clear()
    {
        Ring& ring = Get();
        ring.next = 0;
        ring.count = 0;
        ring.groupIndex.clear();
        ring.groups.clear();
        ++ring.clearEpoch;
        // **판번호는 되돌리지 않는다.** 보던 쪽은 "달라졌다" 로 읽어야 다시 읽는다.
        ++ring.revision;
    }

    void SetEchoToConsole(Bool echo)
    {
        Get().echo = echo;
    }

    Bool GetEchoToConsole()
    {
        return Get().echo;
    }
}
