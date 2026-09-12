#pragma once

#include <JBro/Runtime/ScriptRegistry.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    // 타입 하나를 위한 주소 안정 풀이다. `TObjectPool<T>` 와 같은 일을 하지만
    // 슬롯 크기를 **실행 시간에** 받는다 — 스크립트 타입은 DLL 안에 있어 호스트가
    // 컴파일 시간에 알 수 없기 때문이다(D-42 의 Tier 분리가 만든 제약).
    //
    // 청크 단위로 잡고 청크 안에서 주소가 움직이지 않는다. 해제한 슬롯은 다시 쓰되
    // 제어 블록은 참조가 남아 있으면 재활용하지 않는다(구 엔진의 규칙과 같다).
    class ScriptPool final
    {
    public:
        ScriptPool() = default;
        ~ScriptPool();
        ScriptPool(const ScriptPool&) = delete;
        ScriptPool& operator=(const ScriptPool&) = delete;

        bool Initialize(const ScriptTypeInfo& type, JAllocator allocator, std::size_t chunkSize = 64);
        void Clear();

        GameScriptBase* Create();
        bool Destroy(GameScriptBase* script);

        const ScriptTypeInfo& GetType() const;
        std::size_t GetLiveCount() const;

        template<typename Fn>
        void ForEachLive(Fn&& function)
        {
            for (std::size_t index = 0; index < m_slots.Size(); ++index)
            {
                SlotInfo& slot = m_slots[index];
                if (slot.alive && slot.script != nullptr)
                {
                    function(*slot.script);
                }
            }
        }

    private:
        struct SlotInfo
        {
            GameScriptBase*                script = nullptr;
            SafePtrDetail::ControlBlock*   controlBlock = nullptr;
            bool                           alive = false;
        };

        struct Chunk
        {
            std::byte*  memory = nullptr;
            std::size_t bytes = 0;
        };

        void* SlotStorage(std::size_t index);
        bool  GrowOneChunk();
        void  ReleaseChunks();

        ScriptTypeInfo   m_type;
        JAllocator       m_allocator;
        std::size_t      m_chunkSize = 64;
        std::size_t      m_slotStride = 0;
        std::size_t      m_liveCount = 0;
        Array<Chunk>     m_chunks;
        Array<SlotInfo>  m_slots;
        Array<std::size_t> m_freeSlots;
        bool             m_initialized = false;
    };
}
