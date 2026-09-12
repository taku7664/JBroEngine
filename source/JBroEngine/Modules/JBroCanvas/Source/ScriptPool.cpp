#include <JBro/Canvas/ScriptPool.h>

#include <new>

namespace JBro
{
    namespace
    {
        // 풀이 메모리를 소유하므로 제어 블록은 아무것도 지우지 않는다.
        void IgnoreDelete(void*)
        {
        }

        std::size_t AlignUp(std::size_t value, std::size_t alignment)
        {
            const std::size_t remainder = value % alignment;
            return remainder == 0 ? value : value + (alignment - remainder);
        }
    }

    ScriptPool::~ScriptPool()
    {
        Clear();
        ReleaseChunks();
    }

    bool ScriptPool::Initialize(const ScriptTypeInfo& type, JAllocator allocator, std::size_t chunkSize)
    {
        if (m_initialized
            || type.size == 0
            || type.alignment == 0
            || type.Construct == nullptr
            || type.Destruct == nullptr
            || chunkSize == 0
            || allocator.allocate == nullptr
            || allocator.free == nullptr)
        {
            return false;
        }
        m_type = type;
        m_allocator = allocator;
        m_chunkSize = chunkSize;
        // 슬롯마다 정렬을 맞춘다. 이렇게 두면 청크 시작에서 index * stride 로 바로 간다.
        m_slotStride = AlignUp(type.size, type.alignment);
        m_initialized = true;
        return true;
    }

    void ScriptPool::Clear()
    {
        for (SlotInfo& slot : m_slots)
        {
            if (slot.alive && slot.script != nullptr)
            {
                m_type.Destruct(slot.script);
            }
            if (slot.controlBlock != nullptr)
            {
                slot.controlBlock->Alive = false;
                slot.controlBlock->Ptr = nullptr;
                if (slot.controlBlock->SafeCount == 0)
                {
                    delete slot.controlBlock;
                }
            }
            slot = {};
        }
        m_slots.Clear();
        m_freeSlots.Clear();
        m_liveCount = 0;
    }

    void* ScriptPool::SlotStorage(std::size_t index)
    {
        const std::size_t chunkIndex = index / m_chunkSize;
        const std::size_t withinChunk = index % m_chunkSize;
        return m_chunks[chunkIndex].memory + withinChunk * m_slotStride;
    }

    bool ScriptPool::GrowOneChunk()
    {
        const std::size_t bytes = m_slotStride * m_chunkSize;
        void* memory = m_allocator.allocate(m_allocator.userData, bytes, m_type.alignment);
        if (memory == nullptr)
        {
            return false;
        }
        Chunk chunk;
        chunk.memory = static_cast<std::byte*>(memory);
        chunk.bytes = bytes;
        try
        {
            m_chunks.Add(chunk);
        }
        catch (...)
        {
            m_allocator.free(m_allocator.userData, memory);
            return false;
        }
        return true;
    }

    void ScriptPool::ReleaseChunks()
    {
        for (Chunk& chunk : m_chunks)
        {
            if (chunk.memory != nullptr)
            {
                m_allocator.free(m_allocator.userData, chunk.memory);
            }
        }
        m_chunks.Clear();
    }

    GameScriptBase* ScriptPool::Create()
    {
        if (false == m_initialized)
        {
            return nullptr;
        }

        std::size_t slotIndex = 0;
        const bool reused = false == m_freeSlots.IsEmpty();
        if (reused)
        {
            slotIndex = m_freeSlots.Last();
            m_freeSlots.RemoveAt(m_freeSlots.Size() - 1);
        }
        else
        {
            slotIndex = m_slots.Size();
            if (slotIndex / m_chunkSize >= m_chunks.Size() && false == GrowOneChunk())
            {
                return nullptr;
            }
            try
            {
                m_slots.Add({});
            }
            catch (...)
            {
                return nullptr;
            }
        }

        void* storage = SlotStorage(slotIndex);
        GameScriptBase* script = m_type.Construct(storage);
        if (script == nullptr)
        {
            if (reused)
            {
                m_freeSlots.Add(slotIndex);
            }
            return nullptr;
        }

        SlotInfo& slot = m_slots[slotIndex];
        slot.script = script;
        slot.alive = true;
        // 참조가 남은 블록은 재활용하지 않는다. 남의 수명을 살아 있다고 말하게 된다.
        if (slot.controlBlock != nullptr && slot.controlBlock->SafeCount == 0)
        {
            slot.controlBlock->Ptr = script;
            slot.controlBlock->Alive = true;
            slot.controlBlock->Deleter = &IgnoreDelete;
        }
        else
        {
            slot.controlBlock = new (std::nothrow) SafePtrDetail::ControlBlock(script, &IgnoreDelete);
            if (slot.controlBlock == nullptr)
            {
                m_type.Destruct(script);
                slot.script = nullptr;
                slot.alive = false;
                m_freeSlots.Add(slotIndex);
                return nullptr;
            }
        }
        SafePtrDetail::BindSafeFromThisControlBlock(script, slot.controlBlock);
        ++m_liveCount;
        return script;
    }

    bool ScriptPool::Destroy(GameScriptBase* script)
    {
        if (script == nullptr)
        {
            return false;
        }
        for (std::size_t index = 0; index < m_slots.Size(); ++index)
        {
            SlotInfo& slot = m_slots[index];
            if (false == slot.alive || slot.script != script)
            {
                continue;
            }
            m_type.Destruct(script);
            slot.script = nullptr;
            slot.alive = false;
            if (slot.controlBlock != nullptr)
            {
                slot.controlBlock->Alive = false;
                slot.controlBlock->Ptr = nullptr;
                if (slot.controlBlock->SafeCount != 0)
                {
                    // 아직 누가 보고 있다. 블록은 그들이 놓을 때까지 두고 슬롯만 되돌린다.
                    slot.controlBlock = nullptr;
                }
            }
            m_freeSlots.Add(index);
            --m_liveCount;
            return true;
        }
        return false;
    }

    const ScriptTypeInfo& ScriptPool::GetType() const
    {
        return m_type;
    }

    std::size_t ScriptPool::GetLiveCount() const
    {
        return m_liveCount;
    }
}
