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
        // 이 모듈에 정적 링크된 사본. 호스트가 스크립트 DLL 에 넘겨줄 대상이기도 하다.
        static InstanceRegistry& Local();
        // 실제로 쓰는 레지스트리. 바인딩된 것이 있으면 그것, 없으면 Local() 이다.
        // 호스트와 게임 DLL 은 Runtime 을 각각 정적 링크하므로, 바인딩이 없으면 레지스트리가
        // 두 개가 되어 DLL 안의 Ref<T>·GameObjectHandle 이 아무것도 해석하지 못한다(D-44).
        static InstanceRegistry& Get();
        // Main-thread only. 로드 시 1회만 부른다. nullptr 이면 Local() 로 되돌린다.
        static void Bind(InstanceRegistry* registry);

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
