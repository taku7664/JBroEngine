#include "JoltRuntime.h"

#include <JBro/Core/Log.h>
#include <JBro/Physics3D/World.h>

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/RegisterTypes.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <JBro/Types/Bool.h>

namespace JBro::Physics3D
{
    namespace
    {
        // ── Jolt 의 할당 ──────────────────────────────────────────────
        // Jolt 는 몸이 움직이면 브로드페이즈 트리를 다시 지으며(`QuadTree::UpdatePrepare`) **스텝마다** 작은 배열을 받았다 놓는다.
        // Jolt 의 구조라 피할 수 없다. 그래서 훅 뒤에 크기 칸마다 놓인 블록을 모아 두는 재사용 풀을 둔다 - 같은 크기를 다시 달라고 하면
        // 모아 둔 것을 준다. 자리가 잡힌 뒤로는 스텝이 OS 힙에 손대지 않는다(ProjectRule: 프레임 경로에 힙 할당 금지).
        // 모아 둔 블록은 프로세스가 끝날 때까지 돌려주지 않는다. 훅은 Jolt 의 워커에서도 불리므로 잠금 하나로 지킨다.
        constexpr std::size_t HeaderSize = 16;
        constexpr std::uint32_t SmallestClassBytes = 16;
        // 16 B 부터 64 KiB 까지 두 배씩. 더 큰 것은 풀에 넣지 않는다(바디 표처럼 한 번 받는 것들이다).
        constexpr std::uint32_t ClassCount = 13;

        enum class BlockKind : std::uint32_t
        {
            Pooled = 0x4C4F4F50u,
            Large = 0x4752414Cu,
            OverAligned = 0x4E474C41u,
        };

        // 돌려준 포인터 바로 앞 16 바이트다. 놓을 때 이것을 보고 어디로 돌려보낼지 안다.
        struct BlockHeader
        {
            BlockKind kind;
            std::uint32_t sizeClass;
            void* original;
        };
        static_assert(sizeof(BlockHeader) <= HeaderSize, "a block header must fit before the 16-byte aligned payload");

        // 훅을 지난 횟수와, 그중 풀에 없어 OS 힙에서 받은 횟수.
        std::atomic<std::uint64_t> g_allocations{0};
        std::atomic<std::uint64_t> g_systemAllocations{0};
        std::atomic<std::uint64_t> g_frees{0};
        std::mutex g_poolLock;
        void* g_freeLists[ClassCount] = {};
        // 살아 있는 월드 수. 메인 스레드에서만 바뀐다.
        int g_users = 0;

        BlockHeader* HeaderOf(void* payload)
        {
            return reinterpret_cast<BlockHeader*>(static_cast<std::uint8_t*>(payload) - HeaderSize);
        }

        void* PayloadOf(void* header)
        {
            return static_cast<std::uint8_t*>(header) + HeaderSize;
        }

        std::uint32_t ClassFor(std::size_t size)
        {
            std::uint32_t sizeClass = 0;
            std::size_t bytes = SmallestClassBytes;
            while (bytes < size && sizeClass < ClassCount)
            {
                bytes <<= 1;
                ++sizeClass;
            }
            return sizeClass;
        }

        void* PoolAllocate(std::size_t size)
        {
            g_allocations.fetch_add(1, std::memory_order_relaxed);
            const std::uint32_t sizeClass = ClassFor(size);
            if (sizeClass >= ClassCount)
            {
                g_systemAllocations.fetch_add(1, std::memory_order_relaxed);
                void* raw = std::malloc(HeaderSize + size);
                if (raw == nullptr)
                {
                    return nullptr;
                }
                *static_cast<BlockHeader*>(raw) = {BlockKind::Large, 0, raw};
                return PayloadOf(raw);
            }
            {
                std::lock_guard<std::mutex> lock(g_poolLock);
                void* payload = g_freeLists[sizeClass];
                if (payload != nullptr)
                {
                    // 놓인 블록의 첫 8 바이트가 다음 놓인 블록이다.
                    std::memcpy(&g_freeLists[sizeClass], payload, sizeof(void*));
                    return payload;
                }
            }
            g_systemAllocations.fetch_add(1, std::memory_order_relaxed);
            void* raw = std::malloc(HeaderSize + (static_cast<std::size_t>(SmallestClassBytes) << sizeClass));
            if (raw == nullptr)
            {
                return nullptr;
            }
            *static_cast<BlockHeader*>(raw) = {BlockKind::Pooled, sizeClass, raw};
            return PayloadOf(raw);
        }

        void PoolFree(void* payload)
        {
            if (payload == nullptr)
            {
                return;
            }
            g_frees.fetch_add(1, std::memory_order_relaxed);
            BlockHeader* header = HeaderOf(payload);
            switch (header->kind)
            {
            case BlockKind::Pooled:
            {
                std::lock_guard<std::mutex> lock(g_poolLock);
                std::memcpy(payload, &g_freeLists[header->sizeClass], sizeof(void*));
                g_freeLists[header->sizeClass] = payload;
                return;
            }
            case BlockKind::Large:
            case BlockKind::OverAligned:
                std::free(header->original);
                return;
            }
        }

        void* PoolReallocate(void* block, std::size_t oldSize, std::size_t newSize)
        {
            void* moved = PoolAllocate(newSize);
            if (moved != nullptr && block != nullptr)
            {
                std::memcpy(moved, block, oldSize < newSize ? oldSize : newSize);
            }
            PoolFree(block);
            return moved;
        }

        void* PoolAlignedAllocate(std::size_t size, std::size_t alignment)
        {
            // 풀의 블록은 16 바이트에 맞는다(malloc 이 16 에 맞추고 머리가 16 이다). 그보다 큰 맞춤만 따로 받는다.
            if (alignment <= HeaderSize)
            {
                return PoolAllocate(size);
            }
            g_allocations.fetch_add(1, std::memory_order_relaxed);
            g_systemAllocations.fetch_add(1, std::memory_order_relaxed);
            void* raw = std::malloc(size + alignment + HeaderSize);
            if (raw == nullptr)
            {
                return nullptr;
            }
            const std::uintptr_t first = reinterpret_cast<std::uintptr_t>(raw) + HeaderSize;
            const std::uintptr_t aligned = (first + alignment - 1) & ~(static_cast<std::uintptr_t>(alignment) - 1);
            void* payload = reinterpret_cast<void*>(aligned);
            *HeaderOf(payload) = {BlockKind::OverAligned, 0, raw};
            return payload;
        }

        void TraceToLog(const char* format, ...)
        {
            char line[512];
            va_list arguments;
            va_start(arguments, format);
            std::vsnprintf(line, sizeof(line), format, arguments);
            va_end(arguments);
            Log::Write(LogLevel::Info, "physics3d", "Jolt: %s", line);
        }

        // 타입 팩토리는 Jolt 가 전역 포인터로 찾는다. 힙에 두지 않고 이 자리에 둔다.
        JPH::Factory& FactoryStorage()
        {
            static JPH::Factory factory;
            return factory;
        }
    }

    AllocationStats GetAllocationStats()
    {
        AllocationStats stats;
        stats.allocations = g_allocations.load(std::memory_order_relaxed);
        stats.systemAllocations = g_systemAllocations.load(std::memory_order_relaxed);
        stats.frees = g_frees.load(std::memory_order_relaxed);
        return stats;
    }

    namespace Internal
    {
        JoltRuntime::JoltRuntime()
        {
            if (g_users++ != 0)
            {
                return;
            }
            JPH::Allocate = &PoolAllocate;
            JPH::Reallocate = &PoolReallocate;
            JPH::Free = &PoolFree;
            JPH::AlignedAllocate = &PoolAlignedAllocate;
            JPH::AlignedFree = &PoolFree;
            JPH::Trace = &TraceToLog;
            JPH::Factory::sInstance = &FactoryStorage();
            JPH::RegisterTypes();
        }

        JoltRuntime::~JoltRuntime()
        {
            if (--g_users != 0)
            {
                return;
            }
            JPH::UnregisterTypes();
            JPH::Factory::sInstance = nullptr;
            // 할당 훅은 그대로 둔다 - 정적 객체(팩토리의 표)가 프로세스가 끝날 때 이 훅으로 놓아준다.
        }
    }
}
