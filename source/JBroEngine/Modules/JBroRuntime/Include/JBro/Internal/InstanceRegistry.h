#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Table.h>

#include <cstddef>
#include <cstdint>

namespace JBro::Internal
{
    // 프로세스에서 활성화된 실 객체를 슬롯·세대로 찾는 메인 스레드 전용 레지스트리.
    class InstanceRegistry final
    {
    public:
        static InstanceRegistry& Get();

        InstanceHandle Register(
            InstanceId objectId,
            InstanceId componentId,
            RefCategory category,
            void* pointer);
        bool Unregister(InstanceHandle handle);

        void* Resolve(InstanceHandle handle, RefCategory category) const;
        ResolvedInstance Resolve(
            InstanceId objectId,
            InstanceId componentId,
            RefCategory category) const;

        void Clear();
        std::size_t GetLiveCount() const;

        void ResetDiagnostics();
        std::size_t GetPersistentLookupCount() const;

    private:
        struct Entry
        {
            void* Pointer = nullptr;
            InstanceId ObjectId = InvalidInstanceId;
            InstanceId ComponentId = InvalidInstanceId;
            std::uint32_t Generation = 1;
            RefCategory Category = RefCategory::Object;
            bool Alive = false;
        };

        InstanceRegistry();

        static InstanceId GetPersistentId(
            InstanceId objectId,
            InstanceId componentId,
            RefCategory category);
        static std::uint32_t NextGeneration(std::uint32_t generation);

        Array<Entry> m_entries;
        Array<std::uint32_t> m_freeSlots;
        Table<InstanceId, std::uint32_t> m_idToSlot;
        std::size_t m_liveCount = 0;
        mutable std::size_t m_persistentLookupCount = 0;
    };
}
