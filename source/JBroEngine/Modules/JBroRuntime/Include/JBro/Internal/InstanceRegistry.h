#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Table.h>

#include <cstddef>
#include <cstdint>

namespace JBro::Internal
{
    // 오브젝트 참조 필드의 글자를 파일 안 번호와 오가게 하는 문맥이다(D-233). 캔버스 파일이 쓰고 읽는 동안에만 선다.
    // 레지스트리가 들고 있는 까닭은 레지스트리가 호스트와 스크립트 DLL 이 함께 보는 유일한 것이기 때문이다(D-44).
    struct ObjectRefRemap
    {
        void* user = nullptr;
        // 쓰기: 오브젝트 번호를 파일 안 번호로. 이 캔버스에 없으면 -1.
        std::int64_t (*toIndex)(void* user, InstanceId objectId) = nullptr;
        // 읽기: 파일 안 번호를 오브젝트 번호로. 없으면 InvalidInstanceId.
        InstanceId (*toObjectId)(void* user, std::int64_t index) = nullptr;
    };

    // 프로세스에서 활성화된 실 객체를 슬롯·세대로 찾는 메인 스레드 전용 레지스트리.
    class InstanceRegistry final
    {
    public:
        // 이 모듈에 정적 링크된 사본. 호스트가 스크립트 DLL 에 넘겨줄 대상이기도 하다.
        static InstanceRegistry& Local();
        // 실제로 쓰는 레지스트리. 바인딩된 것이 있으면 그것, 없으면 Local() 이다.
        // 호스트와 게임 DLL 은 Runtime 을 각각 정적 링크하므로, 바인딩이 없으면 레지스트리가
        // 두 개가 되어 DLL 안의 Ref<T>·Handle::GameObject 가 아무것도 해석하지 못한다(D-44).
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

        // 지금 선 오브젝트 참조 문맥이다(D-233). `ObjectRefRemapScope` 가 세우고 거둔다.
        void SetObjectRefRemap(const ObjectRefRemap* remap)
        {
            m_objectRefRemap = remap;
        }

        const ObjectRefRemap* GetObjectRefRemap() const
        {
            return m_objectRefRemap;
        }

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
        // 맨 뒤에 둔다 - 옛 DLL 이 앞 멤버의 자리를 그대로 본다.
        const ObjectRefRemap* m_objectRefRemap = nullptr;
    };
}
