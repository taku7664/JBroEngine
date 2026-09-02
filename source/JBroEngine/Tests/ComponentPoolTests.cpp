#include <JBro/Core/ECS/ComponentPool.h>
#include <JBro/Core/ECS/ComponentRegistry.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    struct TrackingHeap
    {
        std::size_t allocations = 0;
        std::size_t frees = 0;

        static void* Allocate(void* userData, std::size_t size, std::size_t alignment)
        {
            TrackingHeap& heap = *static_cast<TrackingHeap*>(userData);
            ++heap.allocations;
#if defined(_MSC_VER)
            return _aligned_malloc(size, alignment);
#else
            const std::size_t alignedSize = (size + alignment - 1) / alignment * alignment;
            return std::aligned_alloc(alignment, alignedSize);
#endif
        }

        static void Free(void* userData, void* memory)
        {
            TrackingHeap& heap = *static_cast<TrackingHeap*>(userData);
            ++heap.frees;
#if defined(_MSC_VER)
            _aligned_free(memory);
#else
            std::free(memory);
#endif
        }

        JBro::Engine::JAllocator GetAllocator()
        {
            return JBro::Engine::JAllocator{ this, &Allocate, &Free, nullptr };
        }
    };

    struct alignas(64) TestComponent
    {
        explicit TestComponent(int initialValue)
            : value(initialValue)
        {
            ++liveCount;
        }

        ~TestComponent()
        {
            --liveCount;
            ++destroyCount;
        }

        TestComponent(const TestComponent&) = delete;
        TestComponent& operator=(const TestComponent&) = delete;

        int value = 0;
        std::byte padding[60]{};
        static inline int liveCount = 0;
        static inline int destroyCount = 0;
    };

    void TestChunkGrowthReuseAndDenseIteration()
    {
        TrackingHeap heap;
        {
            JBro::Engine::TComponentPool<TestComponent, 32> pool(heap.GetAllocator());
            Check(pool.Reserve(65), "reserve must allocate enough chunks");
            Check(pool.GetCapacity() == 96, "pool capacity must grow by whole chunks");

            std::vector<TestComponent*> components;
            for (int index = 0; index < 70; ++index)
            {
                components.push_back(pool.Create(index));
                Check(components.back() != nullptr, "component construction must succeed");
                Check(reinterpret_cast<std::uintptr_t>(components.back()) % alignof(TestComponent) == 0,
                    "component address must satisfy over-aligned types");
            }
            Check(components.front()->value == 0 && components.back()->value == 69,
                "chunk growth must not move existing components");

            TestComponent* released = components[15];
            Check(pool.Destroy(released), "live component must be destroyable");
            Check(false == pool.Destroy(released), "double destroy must be rejected");

            TestComponent* reused = pool.Create(700);
            Check(reused == released, "most recently released slot must be reused");

            std::size_t visited = 0;
            int valueSum = 0;
            pool.ForEachLive([&](TestComponent& component)
            {
                ++visited;
                valueSum += component.value;
            });
            Check(visited == 70, "dense iteration must visit live components only");
            Check(valueSum > 0, "dense iteration must expose component values");

            pool.Clear();
            Check(pool.GetLiveCount() == 0, "clear must destroy every live component");
            Check(TestComponent::liveCount == 0, "component destructors must all run");
        }

        Check(heap.allocations == heap.frees, "pool must return every allocator-owned block");
    }

    void TestExplicitComponentTypeRegistration()
    {
        JBro::Engine::CComponentRegistry registry;
        const JBro::Engine::ComponentTypeInfo info{ JBro::Engine::ComponentTypeId{ 7 }, sizeof(TestComponent), alignof(TestComponent) };

        Check(registry.Register(info), "valid component type must register");
        Check(registry.Register(info), "identical registration must be idempotent");
        Check(false == registry.Register({ info.type, sizeof(TestComponent) + 1, alignof(TestComponent) }),
            "same type id with a different layout must be rejected");
        Check(false == registry.Register({ JBro::Engine::ComponentTypeId{}, sizeof(TestComponent), alignof(TestComponent) }),
            "zero component type id must be rejected");
        Check(registry.GetTypeCount() == 1, "registry must keep one canonical descriptor per type id");
        Check(registry.Find(info.type) != nullptr, "registered component type must be discoverable");
    }
}

int RunComponentPoolTests()
{
    TestChunkGrowthReuseAndDenseIteration();
    TestExplicitComponentTypeRegistration();
    std::cout << "Component pool tests passed.\n";
    return 0;
}
