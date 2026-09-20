#include <JBro/Core/Log.h>

#include <cstdio>
#include <cstring>

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
            std::uint64_t revision = 0;
            std::uint64_t serial = 0;
            bool echo = true;
        };

        Ring& Get()
        {
            // 프로세스에 하나다. 엔진 인스턴스가 여럿이어도 사람이 보는 로그는 한 줄기다.
            static Ring ring;
            return ring;
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

    void WriteV(LogLevel level, const char* category, const char* format, std::va_list args)
    {
        Ring& ring = Get();
        LogEntry& entry = ring.entries[ring.next];
        entry.level = level;
        entry.serial = ++ring.serial;
        CopyBounded(entry.category, LogEntry::MaxCategory, category);

        if (format == nullptr)
        {
            entry.message[0] = '\0';
        }
        else
        {
            // `vsnprintf` 는 잘려도 늘 널로 끝낸다. 잘린 것은 그 자리에서 알린다 -
            // 조용히 자르면 뒷말이 없어진 줄을 원문으로 읽게 된다.
            const int written = std::vsnprintf(
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

    void Write(LogLevel level, const char* category, const char* format, ...)
    {
        std::va_list args;
        va_start(args, format);
        WriteV(level, category, format, args);
        va_end(args);
    }

    std::uint64_t GetRevision()
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

    void Clear()
    {
        Ring& ring = Get();
        ring.next = 0;
        ring.count = 0;
        // **판번호는 되돌리지 않는다.** 보던 쪽은 "달라졌다" 로 읽어야 다시 읽는다.
        ++ring.revision;
    }

    void SetEchoToConsole(bool echo)
    {
        Get().echo = echo;
    }

    bool GetEchoToConsole()
    {
        return Get().echo;
    }
}
