#include <JBro/Types/Array.h>
#include <JBro/Types/LinearAllocator.h>
#include <JBro/Types/Table.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    void TestStatelessPolicyCostsNothing()
    {
        // D-52 의 전제다. 정책을 멤버로 들어도 기본 컨테이너가 커지면 안 된다.
        struct BareArray
        {
            void* data;
            std::size_t size;
            std::size_t capacity;
        };
        Check(sizeof(JBro::Array<int>) == sizeof(BareArray),
            "a stateless allocator policy must not grow the container");
        Check(sizeof(JBro::Array<int, JBro::JAllocatorRef>) == sizeof(BareArray) + sizeof(void*),
            "a referencing policy must cost exactly one pointer");
    }

    void TestLinearAllocatorHandsOutAndRewinds()
    {
        JBro::LinearAllocator arena;
        Check(false == arena.IsInitialized(), "a fresh arena must hold no block");
        Check(false == arena.Initialize(0), "a zero-byte arena must be rejected");
        Check(arena.Initialize(4096), "the arena must take its block");
        Check(arena.Initialize(4096) == false, "a second initialize must not replace a live block");
        Check(arena.GetCapacity() == 4096 && arena.GetUsedBytes() == 0, "a fresh arena must be empty");

        JBro::JAllocator handle = arena.GetInterface();
        Check(handle.allocate != nullptr && handle.userData == &arena, "the interface must point back at the arena");

        void* first = handle.allocate(handle.userData, 100, 16);
        Check(first != nullptr, "the arena must serve a request inside its budget");
        Check(reinterpret_cast<std::uintptr_t>(first) % 16 == 0, "the arena must honour the requested alignment");
        const std::size_t afterFirst = arena.GetUsedBytes();
        Check(afterFirst >= 100, "serving must advance the cursor");

        void* second = handle.allocate(handle.userData, 8, 64);
        Check(second != nullptr && reinterpret_cast<std::uintptr_t>(second) % 64 == 0,
            "a stricter alignment must still be honoured");
        Check(second != first && arena.GetUsedBytes() > afterFirst, "a second request must not reuse the first block");

        // 개별 해제는 아무 일도 하지 않는다. 커서는 그대로다.
        const std::size_t beforeFree = arena.GetUsedBytes();
        handle.free(handle.userData, first);
        Check(arena.GetUsedBytes() == beforeFree, "freeing one block must not move the cursor");

        const std::size_t peak = arena.GetPeakUsedBytes();
        arena.Reset();
        Check(arena.GetUsedBytes() == 0, "reset must rewind the cursor");
        Check(arena.GetPeakUsedBytes() == peak, "reset must keep the peak for budgeting");
        void* afterReset = handle.allocate(handle.userData, 100, 16);
        Check(afterReset == first, "the first request after a reset must reuse the start of the block");
    }

    void TestArenaOverflowFallsBackAndIsReclaimed()
    {
        JBro::LinearAllocator arena;
        Check(arena.Initialize(128), "the small arena must take its block");
        JBro::JAllocator handle = arena.GetInterface();

        void* inside = handle.allocate(handle.userData, 64, 8);
        Check(inside != nullptr && arena.GetOverflowCount() == 0, "a request inside the budget must not overflow");

        void* outside = handle.allocate(handle.userData, 4096, 8);
        Check(outside != nullptr, "a request past the budget must still be served");
        Check(arena.GetOverflowCount() == 1, "the overflow must be counted");
        Check(arena.GetRequestCount() == 2, "every request must be counted");

        // 넘친 블록도 되감기가 거둔다. 여기서 새면 프레임마다 누수가 된다.
        arena.Reset();
        Check(arena.GetUsedBytes() == 0, "reset must rewind after an overflow too");
        Check(arena.GetOverflowCount() == 1, "reset must keep the overflow count for budgeting");
        arena.Shutdown();
        Check(false == arena.IsInitialized(), "shutdown must release the block");
    }

    void TestContainersDrawFromTheArena()
    {
        JBro::LinearAllocator arena;
        Check(arena.Initialize(1 << 16), "the container arena must take its block");
        const JBro::JAllocator handle = arena.GetInterface();

        {
            JBro::Array<int, JBro::JAllocatorRef> values{JBro::JAllocatorRef(handle)};
            values.Reserve(256);
            Check(arena.GetUsedBytes() >= 256 * sizeof(int),
                "an array bound to the arena must take its storage from the arena");
            for (int index = 0; index < 256; ++index)
            {
                values.Add(index);
            }
            Check(values.Size() == 256 && values[255] == 255, "arena-backed storage must behave like any other");
            Check(arena.GetOverflowCount() == 0, "a reservation inside the budget must not overflow");
        }

        // 컨테이너가 죽어도 커서는 돌아오지 않는다. 그게 선형 할당기다.
        const std::size_t afterScope = arena.GetUsedBytes();
        Check(afterScope > 0, "destroying an arena-backed container must not rewind the cursor");
        arena.Reset();
        Check(arena.GetUsedBytes() == 0, "only reset rewinds the cursor");

        {
            JBro::Table<int, int, JBro::Hash<int>, JBro::EqualTo<>, JBro::JAllocatorRef> table{
                JBro::JAllocatorRef(handle)};
            table.Reserve(64);
            for (int index = 0; index < 64; ++index)
            {
                table.TryAdd(index, index * 2);
            }
            Check(table.Size() == 64, "an arena-backed table must hold its entries");
            const int* found = table.Find(31);
            Check(found != nullptr && *found == 62, "an arena-backed table must find what it stored");
            Check(arena.GetUsedBytes() > 0, "the table must have drawn from the arena");
        }
    }

    void TestGrowthAndCopyKeepTheRightAllocator()
    {
        JBro::LinearAllocator arena;
        Check(arena.Initialize(1 << 16), "the growth arena must take its block");
        const JBro::JAllocator handle = arena.GetInterface();

        // 재해싱은 정책을 잃기 쉽다. 예약 없이 키워서 아레나가 계속 자라는지 본다.
        JBro::Table<int, int, JBro::Hash<int>, JBro::EqualTo<>, JBro::JAllocatorRef> table{
            JBro::JAllocatorRef(handle)};
        std::size_t previousUsed = arena.GetUsedBytes();
        for (int index = 0; index < 400; ++index)
        {
            table.TryAdd(index, index);
            Check(arena.GetUsedBytes() >= previousUsed, "the arena cursor must never go backwards while growing");
            previousUsed = arena.GetUsedBytes();
        }
        Check(table.Size() == 400, "the table must hold every entry across rehashes");
        Check(previousUsed > 400 * sizeof(int),
            "a rehash must keep drawing from the arena instead of falling back to the heap");

        // 아레나에 묶인 배열을 복사해도 사본은 아레나를 물려받지 않는다.
        // 되감기가 사본까지 죽이면 안 되기 때문이다.
        JBro::Array<int, JBro::JAllocatorRef> source{JBro::JAllocatorRef(handle)};
        source.Reserve(128);
        const std::size_t beforeCopy = arena.GetUsedBytes();
        JBro::Array<int, JBro::JAllocatorRef> copy(source);
        copy.Reserve(128);
        Check(arena.GetUsedBytes() == beforeCopy,
            "copying an arena-backed container must not draw from that arena");

        // 대입도 받는 쪽 할당기를 지킨다.
        JBro::Array<int, JBro::JAllocatorRef> heapBacked;
        heapBacked = source;
        Check(arena.GetUsedBytes() == beforeCopy,
            "assigning from an arena-backed container must keep the destination allocator");
    }

    void TestUnboundReferenceFallsBackToTheHeap()
    {
        // 호스트가 memory.frame 을 채우지 않은 프레임에서도 컨테이너는 살아야 한다.
        JBro::Array<int, JBro::JAllocatorRef> values;
        Check(false == JBro::JAllocatorRef{}.IsBound(), "a default reference must report itself unbound");
        values.Reserve(32);
        for (int index = 0; index < 32; ++index)
        {
            values.Add(index);
        }
        Check(values.Size() == 32 && values[31] == 31, "an unbound reference must fall back to the default heap");
    }
}

int RunFrameMemoryTests()
{
    TestStatelessPolicyCostsNothing();
    TestLinearAllocatorHandsOutAndRewinds();
    TestArenaOverflowFallsBackAndIsReclaimed();
    TestContainersDrawFromTheArena();
    TestGrowthAndCopyKeepTheRightAllocator();
    TestUnboundReferenceFallsBackToTheHeap();
    std::cout << "Frame memory tests passed.\n";
    return 0;
}
