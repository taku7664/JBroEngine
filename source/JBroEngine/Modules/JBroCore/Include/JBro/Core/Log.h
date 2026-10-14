#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/IntegerType.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <JBro/Types/UInt.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/String.h>

namespace JBro
{
    // 엔진이 사람에게 하는 말이다(D-133). 기존 엔진 `Engine/Core/Logging/Logger.h` 자리다.
    //
    // **왜 필요한가**: 지금까지 엔진의 알림과 경고는 `std::printf` 로 콘솔에 갔다.
    // 에디터를 창으로 띄우면 그 콘솔은 아무도 보지 않는 자리라, 에셋을 못 읽었다거나
    // 감시가 서지 않았다는 말이 그대로 사라졌다. 기존 엔진에는 로그 창이 있었고 없는 것은
    // 이식이 덜 된 자리다.
    //
    // **발생 이력은 고리 버퍼 하나다.** 오래된 것부터 밀린다. 명시적 묶음의 요약은
    // Clear 전까지 유지해 총횟수를 잃지 않는다. 따라서 서로 다른 키의 수만큼 요약 메모리가 늘며,
    // 프레임에서 반복되는 오류 키는 스크립트 위치처럼 안정된 값이어야 한다.
    //
    // **메인 스레드 전용이다.** 워커에서 부르지 않는다. 잠금을 두지 않는 대신 그 계약을 지킨다.
    enum class LogLevel : std::uint8_t
    {
        Trace,
        Debug,
        Info,
        Warning,
        Error
    };

    struct LogEntry
    {
        // 한 줄이 이보다 길면 잘린다. 잘린 줄은 `...` 로 끝난다.
        static constexpr std::size_t MaxMessage = 512;
        // 어디서 온 말인가(`asset`·`editor`·`script` 같은 것). 거르는 데 쓴다.
        static constexpr std::size_t MaxCategory = 32;

        LogLevel level = LogLevel::Info;
        // 이 줄이 몇 번째로 쌓인 것인가. 고리가 돌아도 이 값은 늘 늘어난다.
        UInt64 serial = 0;
        // UTC Unix milliseconds. Each occurrence owns its time even when grouped.
        UInt64 timestampMs = 0;
        // Zero means this line was not explicitly grouped.
        UInt64 groupId = 0;
        char timeText[24] = {};
        char category[MaxCategory] = {};
        char message[MaxMessage] = {};
        // Cached panel row; no formatting or allocation while drawing the list.
        char display[608] = {};
    };

    struct LogGroup
    {
        UInt64 id = 0;
        String key;
        UInt64 count = 0;
        UInt64 firstSerial = 0;
        UInt64 latestSerial = 0;
        UInt64 firstTimestampMs = 0;
        UInt64 latestTimestampMs = 0;
        LogLevel latestLevel = LogLevel::Info;
        char latestTimeText[24] = {};
        char display[640] = {};
    };

    namespace Log
    {
        // 펼친 발생 이력의 최대 줄 수. 넘치면 오래된 것부터 밀린다.
        inline constexpr std::size_t Capacity = 10000;

        void WriteFormatted(LogLevel level, const char* category, const char* format, ...);
        void WriteV(LogLevel level, const char* category, const char* format, std::va_list args);
        // Grouping is opt-in. A nonempty key identifies one group for this process session,
        // regardless of message or object; nullptr/empty is the ordinary ungrouped path.
        void WriteGroupedFormatted(LogLevel level, const char* category, const char* key, const char* format, ...);
        void WriteGroupedV(LogLevel level, const char* category, const char* key, const char* format, std::va_list args);

        namespace Detail
        {
            template<typename T>
            struct IsIntegerValue : std::false_type
            {
            };

            template<typename U>
            struct IsIntegerValue<IntegerType<U>> : std::true_type
            {
            };

            // **엔진 값 타입은 가변 인자로 그대로 넘기지 않는다.** `Float` 는 `float` 처럼 `double` 로
            // 올라가지 않아 `%f` 가 쓰레기를 읽는다 - 컴파일러는 클래스라 형식 검사도 하지 않는다(D-290).
            // 여기서 원시 값으로 내려 준다. 나머지는 그대로 넘긴다.
            template<typename T>
            constexpr decltype(auto) ToVarArg(const T& value) noexcept
            {
                if constexpr (std::is_same_v<T, Float>)
                {
                    return static_cast<double>(value.Get());
                }
                else if constexpr (std::is_same_v<T, Bool>)
                {
                    // 가변 인자는 경계다 - `Int32` 객체를 넘기면 클래스가 그대로 간다.
                    return static_cast<int>(value.Get());
                }
                else if constexpr (IsIntegerValue<T>::value)
                {
                    return value.Get();
                }
                else
                {
                    return (value);
                }
            }
        }

        template<typename... Args>
        void Write(LogLevel level, const char* category, const char* format, const Args&... args)
        {
            WriteFormatted(level, category, format, Detail::ToVarArg(args)...);
        }

        template<typename... Args>
        void WriteGrouped(LogLevel level, const char* category, const char* key, const char* format, const Args&... args)
        {
            WriteGroupedFormatted(level, category, key, format, Detail::ToVarArg(args)...);
        }

        // 줄이 쌓일 때마다 늘어난다. 로그 창은 이 값이 그대로면 다시 읽지 않는다.
        UInt64 GetRevision();
        // 지금 고리에 남아 있는 줄 수(`Capacity` 를 넘지 않는다).
        std::size_t GetCount();
        // `index` 는 0 이 가장 오래된 줄이다. 범위를 넘으면 nullptr 이다.
        const LogEntry* GetAt(std::size_t index);
        std::size_t GetGroupCount();
        const LogGroup* GetGroupAt(std::size_t index);
        UInt64 GetSessionId();
        UInt64 GetClearEpoch();
        void Clear();

        // **콘솔에도 내보낼 것인가.** 게임 실행은 참이 기본이고, 그래야 창 없이 돌릴 때
        // 볼 수 있다. 테스트가 조용히 돌리려고 끄는 자리이기도 하다.
        void SetEchoToConsole(Bool echo);
        Bool GetEchoToConsole();
    }
}
