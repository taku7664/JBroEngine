#pragma once

#include <JBro/Core/Core.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace JBro
{
    // 청크 기반 오브젝트 풀. 요구:
    //   1) 한 번 발급된 T* 는 다른 T 가 추가돼도 이동하지 않는다(주소 안정성).
    //   2) 파괴는 슬롯을 free-list 로 되돌리고 다음 Create 가 재사용한다.
    //   3) ForEachLive 는 살아 있는 원소만 순회한다(밀집 순회는 아니지만 스킵 저렴).
    // 실제 구현은 F1~G4 단계에서 채운다. 여기서는 시그니처만 확정한다.
    template <typename T, std::size_t ChunkSize = 32>
    class TObjectPool
    {
    public:
        explicit TObjectPool(JAllocator allocator) : m_allocator(allocator) {}
        ~TObjectPool() = default;

        TObjectPool(const TObjectPool&)            = delete;
        TObjectPool& operator=(const TObjectPool&) = delete;

        template <typename... Args>
        T*     Create(Args&&... args);
        bool   Destroy(T* value);
        void   Clear();
        bool   Reserve(std::size_t requestedCapacity);

        std::size_t GetCapacity()  const;
        std::size_t GetLiveCount() const;

        template <typename Fn>
        void ForEachLive(Fn&& function);

    private:
        JAllocator m_allocator;
        // 청크·free-list·라이브 카운트 등의 실 필드는 구현 단계에서 채운다.
    };
}
