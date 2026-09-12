#include <JBro/Core/InstanceIdGenerator.h>
#include <JBro/Core/ObjectPool.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Types/SafePtr.h>

#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    class SafeTarget final : public JBro::EnableSafeFromThis<SafeTarget>
    {
    public:
        explicit SafeTarget(int value)
            : Value(value)
        {
        }

        int Value = 0;
    };

    struct RegistryTarget
    {
        int Value = 0;
    };

    struct AllocationProbe
    {
        JBro::JAllocator Backing = JBro::CreateDefaultAllocator();
        std::size_t AllocateCalls = 0;
        std::size_t FreeCalls = 0;
    };

    void* CountPoolAllocation(
        void* userData,
        std::size_t size,
        std::size_t alignment)
    {
        auto& probe = *static_cast<AllocationProbe*>(userData);
        ++probe.AllocateCalls;
        return probe.Backing.allocate(
            probe.Backing.userData,
            size,
            alignment);
    }

    void CountPoolFree(void* userData, void* memory)
    {
        auto& probe = *static_cast<AllocationProbe*>(userData);
        ++probe.FreeCalls;
        probe.Backing.free(probe.Backing.userData, memory);
    }

    class TestComponent final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "TestComponent";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        int Value = 0;
    };

    void TestSafePtrExpiresWithOwner()
    {
        JBro::SafePtr<SafeTarget> safe;
        {
            JBro::OwnerPtr<SafeTarget> owner = JBro::MakeOwnerPtr<SafeTarget>(42);
            safe = owner->SafeFromThis();
            Check(safe.IsValid(), "SafePtr must be valid while its owner is alive");
            Check(safe->Value == 42, "SafePtr must access the owned object");
        }

        Check(false == safe.IsValid(), "SafePtr must expire after owner destruction");
        Check(safe.TryGet() == nullptr, "expired SafePtr must return nullptr");
    }

    void TestInstanceIdGeneratorSequenceAndOverflow()
    {
        constexpr std::uint64_t SequenceMask = (1ull << 12) - 1;
        constexpr std::uint64_t SessionMask = (1ull << 10) - 1;

        JBro::InstanceIdGenerator generator;
        generator.BeginFrame();
        const JBro::InstanceId first = generator.Generate();
        const std::uint64_t firstTimestamp = first >> 22;
        const std::uint64_t firstSession = (first >> 12) & SessionMask;
        Check((first & SequenceMask) == 0, "first id in a frame must start at sequence zero");

        JBro::InstanceId previous = first;
        for (std::uint64_t sequence = 1; sequence < 4096; ++sequence)
        {
            const JBro::InstanceId id = generator.Generate();
            Check(id > previous, "ids must be strictly increasing");
            Check((id >> 22) == firstTimestamp, "first 4096 ids must share the cached timestamp");
            Check(((id >> 12) & SessionMask) == firstSession, "session bits must stay stable");
            Check((id & SequenceMask) == sequence, "sequence bits must not repeat before overflow");
            previous = id;
        }

        const JBro::InstanceId overflow = generator.Generate();
        Check((overflow >> 22) == firstTimestamp + 1, "sequence overflow must advance logical time");
        Check((overflow & SequenceMask) == 0, "sequence overflow must restart at zero");
        Check(overflow > previous, "overflow handling must preserve ordering");

        generator.BeginFrame();
        Check(generator.Generate() > overflow, "a new frame must not move logical time backwards");
    }

    void TestObjectPoolAddressStabilityAndLifetime()
    {
        JBro::TObjectPool<SafeTarget> pool(JBro::CreateDefaultAllocator());
        SafeTarget* first = pool.Create(1);
        Check(first != nullptr, "pool must create its first object");
        JBro::SafePtr<SafeTarget> firstSafe = first->SafeFromThis();

        for (int value = 2; value <= 200; ++value)
        {
            Check(pool.Create(value) != nullptr, "pool must grow across multiple chunks");
        }

        Check(pool.GetLiveCount() == 200, "pool must count live objects");
        Check(pool.GetCapacity() >= 200, "pool capacity must cover every live object");
        Check(first->Value == 1, "pool growth must not move or overwrite the first object");

        std::size_t visited = 0;
        bool firstAddressPreserved = false;
        pool.ForEachLive([&visited, &firstAddressPreserved, first](SafeTarget& target)
        {
            ++visited;
            if (target.Value == 1)
            {
                firstAddressPreserved = &target == first;
            }
        });
        Check(visited == 200, "pool iteration must visit every live object once");
        Check(firstAddressPreserved, "pool growth must preserve the first object address");

        Check(pool.Destroy(first), "pool must destroy a live object");
        Check(false == firstSafe.IsValid(), "pool destruction must invalidate SafePtr");
        Check(pool.GetLiveCount() == 199, "destroy must reduce the live count");

        SafeTarget* reused = pool.Create(201);
        Check(reused == first, "free-list creation must reuse the released slot");
        Check(reused->Value == 201, "reused slot must contain the new object");
        Check(false == firstSafe.IsValid(), "slot reuse must not revive an old SafePtr");
    }

    void TestObjectPoolUsesItsSuppliedAllocatorForChunks()
    {
        AllocationProbe allocations;
        JBro::JAllocator allocator;
        allocator.userData = &allocations;
        allocator.allocate = &CountPoolAllocation;
        allocator.free = &CountPoolFree;

        {
            JBro::TObjectPool<SafeTarget> pool(allocator);
            for (int value = 0; value < 200; ++value)
            {
                Check(pool.Create(value) != nullptr,
                    "custom-allocator pool must create every object");
            }
            Check(allocations.AllocateCalls == 7,
                "200 objects must allocate seven 32-slot chunks through JAllocator");
            Check(allocations.FreeCalls == 0,
                "live pool chunks must not be released early");
        }

        Check(allocations.FreeCalls == allocations.AllocateCalls,
            "pool destruction must return every chunk to its supplied allocator");
    }

    // §9 / D-54: 미리 확보한 풀에서의 스폰·파괴는 힙을 건드리지 않는다.
    // 계약을 문장이 아니라 카운터로 고정한다. 청크와 ControlBlock 둘 다 Reserve 가 잡아 둔다.
    void TestReservedPoolSpawnDoesNotAllocate()
    {
        AllocationProbe allocations;
        JBro::JAllocator allocator;
        allocator.userData = &allocations;
        allocator.allocate = &CountPoolAllocation;
        allocator.free = &CountPoolFree;
        {
            JBro::TObjectPool<SafeTarget> pool(allocator);
            Check(pool.Reserve(256), "reserving the pool must succeed");

            const std::size_t reserved = allocations.AllocateCalls;
            const std::size_t reservedBlocks = pool.GetControlBlockAllocationCount();
            Check(reserved > 0, "reserving must allocate its chunks up front");
            Check(reservedBlocks >= 256,
                "reserving must take its control blocks up front too");

            SafeTarget* spawned[128] = {};
            int seed = 0;
            for (auto& slot : spawned)
            {
                slot = pool.Create(seed++);
                Check(slot != nullptr, "a reserved pool must hand out objects");
            }
            Check(allocations.AllocateCalls == reserved
                && pool.GetControlBlockAllocationCount() == reservedBlocks,
                "spawning inside the reserved capacity must not allocate at all");

            for (auto* value : spawned)
            {
                Check(pool.Destroy(value), "destroying a pooled object must succeed");
            }
            const std::size_t stepsBeforeRespawn = pool.GetSlotSearchStepCount();
            Check(allocations.AllocateCalls == reserved
                && pool.GetControlBlockAllocationCount() == reservedBlocks,
                "destroying must not allocate either");
            // 128회 파괴가 이분 탐색이면 청크 수의 로그에 비례한다. 선형 탐색으로 되돌아가면
            // 살아 있는 객체 수에 비례해 늘어나므로 이 상한을 넘는다.
            Check(stepsBeforeRespawn <= 128 * 8,
                "destroying must locate its slot by address, not by scanning every slot");

            for (auto& slot : spawned)
            {
                slot = pool.Create(seed++);
                Check(slot != nullptr, "respawning must reuse the freed slots");
            }
            Check(allocations.AllocateCalls == reserved
                && pool.GetControlBlockAllocationCount() == reservedBlocks,
                "respawning must reuse the recycled control blocks, not allocate new ones");
        }
        Check(allocations.FreeCalls == allocations.AllocateCalls,
            "the pool must return every chunk it took from its allocator");
    }

    void TestObjectPoolRejectsUnrepresentableCapacity()
    {
        AllocationProbe allocations;
        JBro::JAllocator allocator;
        allocator.userData = &allocations;
        allocator.allocate = &CountPoolAllocation;
        allocator.free = &CountPoolFree;

        JBro::TObjectPool<SafeTarget> pool(allocator);
        Check(false == pool.Reserve(std::numeric_limits<std::size_t>::max()),
            "pool must reject a capacity whose rounded chunk count overflows size_t");
        Check(allocations.AllocateCalls == 0,
            "unrepresentable capacity must fail before requesting memory");
    }

    void TestRefUsesHandleCacheBeforePersistentLookup()
    {
        static_assert(std::is_assignable_v<
            decltype((std::declval<const JBro::InstanceRef&>().Cached)), JBro::InstanceHandle>,
            "runtime cache updates must be legal on a genuinely const reference");
        static_assert(sizeof(JBro::InstanceRef) == 24);
        JBro::Internal::InstanceRegistry& registry =
            JBro::Internal::InstanceRegistry::Get();
        registry.Clear();

        RegistryTarget first{42};
        const JBro::InstanceHandle firstHandle = registry.Register(
            100,
            JBro::InvalidInstanceId,
            JBro::RefCategory::Object,
            &first);
        Check(firstHandle.IsSet(), "registry must return a live handle");

        JBro::Ref<RegistryTarget> reference;
        reference.ObjectId = 100;
        registry.ResetDiagnostics();
        Check(reference.Get() == &first, "first Ref lookup must resolve its persistent id");
        Check(reference.Cached.IsSet(), "first Ref lookup must fill the handle cache");
        Check(registry.GetPersistentLookupCount() == 1, "first Ref lookup must read the id table once");
        Check(reference.Get() == &first, "second Ref lookup must resolve the cached handle");
        Check(registry.GetPersistentLookupCount() == 1, "cache hit must not read the persistent id table");

        JBro::Ref<RegistryTarget> coldReference;
        coldReference.ObjectId = 100;
        const JBro::Ref<RegistryTarget> immutableReference = coldReference;
        Check(immutableReference.Get() == &first && immutableReference.Cached.IsSet(),
            "const Ref must populate its runtime cache without modifying persistent identity");
        const auto constLookups = registry.GetPersistentLookupCount();
        Check(immutableReference.Get() == &first && registry.GetPersistentLookupCount() == constLookups,
            "const Ref cache hit must not repeat persistent lookup");
        Check(immutableReference.ObjectId == 100 && immutableReference.ComponentId == JBro::InvalidInstanceId,
            "const cache update must preserve serialized identity");

        JBro::InstanceRef loadedReference;
        loadedReference.ObjectId = 100;
        loadedReference.Cached = {999, 999};
        Check(JBro::Internal::PatchInstanceRefCache(
            loadedReference,
            JBro::RefCategory::Object),
            "load patchup must resolve persistent ids");
        Check(loadedReference.Cached.Slot == firstHandle.Slot
            && loadedReference.Cached.Gen == firstHandle.Gen,
            "load patchup must replace stale runtime cache values");

        Check(registry.Unregister(firstHandle), "registry must unregister a live handle");
        RegistryTarget replacement{84};
        const JBro::InstanceHandle replacementHandle = registry.Register(
            200,
            JBro::InvalidInstanceId,
            JBro::RefCategory::Object,
            &replacement);
        Check(replacementHandle.Slot == firstHandle.Slot, "registry must reuse a released slot");
        Check(replacementHandle.Gen != firstHandle.Gen, "reused registry slots must change generation");
        Check(reference.Get() == nullptr, "stale Ref must not resolve a different object in a reused slot");
        registry.Clear();
    }

    void TestCanvasObjectComponentAndHandleRoundTrip()
    {
        JBro::Internal::InstanceRegistry& registry =
            JBro::Internal::InstanceRegistry::Get();
        registry.Clear();

        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* parent = canvas.CreateObject("Parent");
        JBro::GameObject* child = canvas.CreateObject("Child");
        Check(parent != nullptr && child != nullptr, "Canvas must create pooled objects");
        Check(canvas.GetObjectCount() == 2, "Canvas must report live objects");
        Check(registry.GetLiveCount() == 2, "created objects must be registered");

        const JBro::LayerIndex originalDefault = canvas.GetDefaultLayer();
        JBro::Layer& replacementLayer = canvas.CreateLayer("Replacement");
        Check(canvas.DestroyLayer(originalDefault), "default layer must be replaceable");
        Check(parent->GetLayer() == &replacementLayer,
            "objects must move before their layer is destroyed");

        child->SetParent(parent);
        TestComponent* first = canvas.AttachComponent<TestComponent>(parent);
        TestComponent* second = canvas.AttachComponent<TestComponent>(parent);
        Check(first != nullptr && second != nullptr, "Canvas must attach pooled components");
        first->Value = 11;
        second->Value = 22;
        Check(first->GetOwner().GetInstanceId() == parent->GetInstanceId(),
            "attached component must retain its owner");
        Check(registry.GetLiveCount() == 4, "attached components must be registered");

        JBro::Ref<TestComponent> found = parent->GetComponent<TestComponent>();
        Check(found.Get() == first, "GetComponent must return the first matching component");
        const JBro::Array<JBro::Ref<TestComponent>> all =
            parent->GetComponents<TestComponent>();
        Check(all.Size() == 2, "GetComponents must return every matching component");
        std::size_t visited = 0;
        int total = 0;
        canvas.ForEach<TestComponent>([&visited, &total](TestComponent& component)
        {
            ++visited;
            total += component.Value;
        });
        Check(visited == 2 && total == 33, "typed Canvas iteration must visit live components");

        JBro::GameObjectHandle handle = parent->GetScriptHandle();
        Check(handle.IsValid(), "handle must resolve a live object");
        Check(handle.GetComponent<TestComponent>().Get() == first,
            "handle component lookup must preserve first-match semantics");
        handle.SetActive(false);
        Check(false == parent->IsActiveSelf(), "handle must safely change active state");
        Check(false == handle.IsActive(), "handle active query must include hierarchy state");
        Check(false == first->IsActiveComponent(),
            "component active gate must include owner hierarchy state");

        JBro::SafePtr<JBro::GameObject> parentSafe = parent->SafeFromThis();
        JBro::SafePtr<JBro::GameObject> childSafe = child->SafeFromThis();
        handle.Destroy();
        Check(false == handle.IsValid(), "destroyed object handle must become invalid");
        Check(false == parentSafe.IsValid() && false == childSafe.IsValid(),
            "recursive object destruction must invalidate SafePtr values");
        Check(false == found.IsValid(), "destroyed component Ref must become invalid");
        Check(canvas.GetObjectCount() == 0, "recursive destruction must empty the object pool");
        Check(registry.GetLiveCount() == 0, "destruction must remove every registry entry");

        handle.SetActive(true);
        handle.Destroy();
        Check(false == handle.IsActive(), "invalid handle access must remain safe");
        registry.Clear();
    }

    void TestCanvasIdsAreUniqueAcrossCanvases()
    {
        JBro::Internal::InstanceRegistry& registry =
            JBro::Internal::InstanceRegistry::Get();
        registry.Clear();

        {
            JBro::Canvas firstCanvas(JBro::CreateDefaultAllocator());
            JBro::Canvas secondCanvas(JBro::CreateDefaultAllocator());
            JBro::GameObject* first = firstCanvas.CreateObject("FirstCanvasObject");
            JBro::GameObject* second = secondCanvas.CreateObject("SecondCanvasObject");
            Check(first != nullptr && second != nullptr,
                "multiple canvases must both create registered objects");
            Check(first->GetInstanceId() != second->GetInstanceId(),
                "Canvas instance ids must be process-unique");
            Check(registry.GetLiveCount() == 2,
                "global registry must retain objects from multiple canvases");
        }

        Check(registry.GetLiveCount() == 0,
            "Canvas teardown must unregister only its own objects");
        registry.Clear();
    }
}

int RunReferenceSafetyTests()
{
    TestSafePtrExpiresWithOwner();
    TestInstanceIdGeneratorSequenceAndOverflow();
    TestObjectPoolAddressStabilityAndLifetime();
    TestObjectPoolUsesItsSuppliedAllocatorForChunks();
    TestReservedPoolSpawnDoesNotAllocate();
    TestObjectPoolRejectsUnrepresentableCapacity();
    TestRefUsesHandleCacheBeforePersistentLookup();
    TestCanvasObjectComponentAndHandleRoundTrip();
    TestCanvasIdsAreUniqueAcrossCanvases();
    std::cout << "Reference safety tests passed.\n";
    return 0;
}
