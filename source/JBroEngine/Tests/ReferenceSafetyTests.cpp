#include <JBro/Core/InstanceIdGenerator.h>
#include <JBro/Core/ObjectPool.h>
#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Types/SafePtr.h>

#include <iostream>
#include <stdexcept>

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

    void TestRefUsesHandleCacheBeforePersistentLookup()
    {
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
}

int RunReferenceSafetyTests()
{
    TestSafePtrExpiresWithOwner();
    TestInstanceIdGeneratorSequenceAndOverflow();
    TestObjectPoolAddressStabilityAndLifetime();
    TestRefUsesHandleCacheBeforePersistentLookup();
    std::cout << "Reference safety tests passed.\n";
    return 0;
}
