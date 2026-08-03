#pragma once

#include "Utillity/Pointer/SafePtr.h"

#include <cstddef>
#include <new>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  TObjectPool<T> — 고정 크기 객체용 청크 풀.
//
//  · 청크 = 슬롯 SlotsPerChunk 개의 고정 배열. 청크는 단방향 연결 → 객체 수가 늘면
//    청크를 이어붙인다. 슬롯 주소는 한 번 할당되면 풀이 살아있는 동안 불변
//    (compaction 없음) → 캐시된 raw 포인터와 SafePtr 가 안전하다.
//  · 외부 공유: 풀에 들어가는 T 는 EnableSafeFromThis<...> 를 상속해야 한다.
//    Allocate 가 ControlBlock 을 만들어 객체에 바인딩하므로, 외부 코드는
//    obj->SafeFromThis() 로 SafePtr 를 얻어 안전하게 보유한다. (Utillity/SafePtr.h
//    는 무수정 — ControlBlock 의 public 멤버와 BindSafeFromThisControlBlock 만 사용.)
//  · 소유: 풀이 메모리(슬롯)와 ControlBlock 수명을 관리한다. 객체 파괴는 풀의
//    Free()/Clear() 가 직접 수행(~T() 호출)하므로, ControlBlock 의 Deleter 는
//    no-op 로 둔다(OwnerPtr 경유 이중 파괴 방지). 파괴 시 block->Alive=false 로
//    살아있는 SafePtr 를 무효화한다.
//  · ControlBlock 재활용: 블록은 슬롯보다 오래 살 수 있어(만료 SafePtr 가 남으면)
//    슬롯에 인라인할 수 없다. 대신 개별 할당을 유지한 채 **풀이 캐시**한다 —
//    파괴 시 참조가 0 이면 heap 으로 돌려주지 않고 free list 에 담아 다음 Allocate 가
//    재사용하고, 참조가 남아 있으면 그 블록은 풀을 떠나(escape) 마지막
//    SafePtr::ReleaseRef 가 delete 한다. 블록이 항상 전역 new/delete 로만 오가므로
//    슬랩/오버로드와 달리 모듈(호스트↔게임 DLL) 경계에서도 안전하고, 흔한 경로에서
//    객체당 malloc/free 한 쌍이 사라진다.
//
//  순회: ForEachLive 는 별도의 dense live-pointer index 만 방문한다(시스템용).
//  이 배열은 순회 비용을 live 수에 비례시키기 위한 저장 인덱스이며 순서를 보장하지 않는다.
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
class TObjectPool final
{
public:
	static constexpr std::size_t SlotsPerChunk = 32;

	TObjectPool() = default;
	~TObjectPool() { Clear(); FreeChunks(); }

	TObjectPool(const TObjectPool&) = delete;
	TObjectPool& operator=(const TObjectPool&) = delete;

	// 슬롯에 std::construct_at 으로 T 를 생성하고 ControlBlock 을 바인딩한다.
	// 외부 안전참조는 호출 측에서 obj->SafeFromThis() 로 얻는다.
	template<typename... Args>
	T* Allocate(Args&&... args)
	{
		static_assert(requires(T* t) { t->SafeFromThis(); },
			"TObjectPool<T>: T 는 EnableSafeFromThis<...> 를 상속해야 한다(외부 SafePtr 노출에 필요).");

		Slot* slot = AcquireSlot();
		if (nullptr == slot)
		{
			return nullptr;
		}

		T* object = std::construct_at(
			reinterpret_cast<T*>(slot->Storage),
			std::forward<Args>(args)...);

		// ControlBlock 은 풀이 직접 관리 — Deleter 는 no-op(파괴는 Free 가 수행).
		SafePtrDetail::ControlBlock* block = AcquireControlBlock(object);
		SafePtrDetail::BindSafeFromThisControlBlock(object, block);

		slot->Block = block;
		slot->Occupied = true;
		slot->LiveIndex = m_liveObjects.size();
		m_liveObjects.push_back(object);
		return object;
	}

	// 객체를 파괴하고 슬롯을 회수한다. obj 는 이 풀에서 Allocate 된 포인터여야 한다.
	void Free(T* object)
	{
		if (nullptr == object)
		{
			return;
		}

		// Storage 가 Slot 의 첫 멤버이므로 object 주소 == Slot 주소.
		Slot* slot = reinterpret_cast<Slot*>(object);
		if (false == slot->Occupied)
		{
			return;
		}

		RemoveLiveObject(*slot);
		DestroySlot(*slot);
		ReleaseSlot(slot);
	}

	void Clear()
	{
		for (T* object : m_liveObjects)
		{
			Slot& slot = *reinterpret_cast<Slot*>(object);
			DestroySlot(slot);
			slot.Occupied = false;
			slot.LiveIndex = InvalidLiveIndex;
		}
		m_liveObjects.clear();
		m_freeHead = nullptr;
		RebuildFreeList();
	}

	template<typename Fn>
	void ForEachLive(Fn&& fn)
	{
		for (T* object : m_liveObjects)
		{
			fn(*object);
		}
	}

	template<typename Fn>
	void ForEachLive(Fn&& fn) const
	{
		for (const T* object : m_liveObjects)
		{
			fn(*object);
		}
	}

	std::size_t GetLiveCount() const { return m_liveObjects.size(); }
	std::size_t GetCapacity() const { return m_chunkCount * SlotsPerChunk; }
	// 재사용 대기 중인 ControlBlock 수(진단/회귀 테스트용 — 재활용이 실제로 도는지 확인).
	std::size_t GetCachedControlBlockCount() const { return m_freeBlocks.size(); }
	void Reserve(std::size_t capacity)
	{
		while (GetCapacity() < capacity)
		{
			if (false == AddChunk())
			{
				break;
			}
		}
	}

private:
	static constexpr std::size_t InvalidLiveIndex = static_cast<std::size_t>(-1);

	struct Slot
	{
		alignas(T) unsigned char     Storage[sizeof(T)];   // 반드시 첫 멤버 (T* ↔ Slot* 역변환)
		SafePtrDetail::ControlBlock* Block    = nullptr;
		Slot*                        NextFree = nullptr;
		std::size_t                  LiveIndex = InvalidLiveIndex;
		bool                         Occupied = false;
	};

	struct Chunk
	{
		Slot   Slots[SlotsPerChunk];
		Chunk* Next = nullptr;
	};

	static void NoopDeleter(void*) {}

	// 재사용 대기 블록이 있으면 되살리고, 없으면 새로 할당한다.
	// 캐시에는 "참조가 0 인 블록" 만 들어오므로(DestroySlot 참조) 되살릴 때 남은 SafePtr 를
	// 되살리는 사고는 없다 — SafeCount 0 = 이 블록을 보는 SafePtr 가 하나도 없다는 뜻이다.
	SafePtrDetail::ControlBlock* AcquireControlBlock(T* object)
	{
		if (false == m_freeBlocks.empty())
		{
			SafePtrDetail::ControlBlock* block = m_freeBlocks.back();
			m_freeBlocks.pop_back();
			// 슬롯이 바뀔 수 있으므로 Ptr 을 반드시 새 객체로 갱신한다.
			block->Ptr = static_cast<void*>(object);
			block->Alive = true;
			block->SafeCount = 0;
			block->Deleter = &NoopDeleter;
			return block;
		}
		return new SafePtrDetail::ControlBlock(static_cast<void*>(object), &NoopDeleter);
	}

	// ControlBlock/객체 파괴(슬롯 점유 상태는 호출자가 정리).
	void DestroySlot(Slot& slot)
	{
		T* object = reinterpret_cast<T*>(slot.Storage);
		object->~T();

		if (SafePtrDetail::ControlBlock* block = slot.Block)
		{
			block->Alive = false;
			block->Ptr = nullptr;
			if (0 == block->SafeCount)
			{
				// 이 블록을 보는 SafePtr 가 없다 — heap 에 돌려주지 않고 재사용 대기로 돌린다.
				m_freeBlocks.push_back(block);
			}
			// 참조가 남아 있으면 블록은 풀을 떠난다 — 만료 판정(Alive=false)에 계속 쓰이다가
			// 마지막 SafePtr::ReleaseRef 가 delete 한다. 여기서 캐시에 담으면 살아 있는
			// 참조를 다음 객체로 되살리게 되므로 절대 담지 않는다.
			slot.Block = nullptr;
		}
	}

	void FreeCachedControlBlocks()
	{
		for (SafePtrDetail::ControlBlock* block : m_freeBlocks)
		{
			delete block;
		}
		m_freeBlocks.clear();
	}

	void RemoveLiveObject(Slot& slot)
	{
		const std::size_t removeIndex = slot.LiveIndex;
		T* movedObject = m_liveObjects.back();
		m_liveObjects[removeIndex] = movedObject;
		m_liveObjects.pop_back();

		if (removeIndex < m_liveObjects.size())
		{
			Slot* movedSlot = reinterpret_cast<Slot*>(movedObject);
			movedSlot->LiveIndex = removeIndex;
		}
		slot.LiveIndex = InvalidLiveIndex;
	}

	Slot* AcquireSlot()
	{
		if (nullptr == m_freeHead && false == AddChunk())
		{
			return nullptr;
		}
		Slot* slot = m_freeHead;
		m_freeHead = slot->NextFree;
		slot->NextFree = nullptr;
		return slot;
	}

	void ReleaseSlot(Slot* slot)
	{
		slot->Occupied = false;
		slot->NextFree = m_freeHead;
		m_freeHead = slot;
	}

	bool AddChunk()
	{
		m_liveObjects.reserve(GetCapacity() + SlotsPerChunk);
		Chunk* chunk = new (std::nothrow) Chunk();
		if (nullptr == chunk)
		{
			return false;
		}
		chunk->Next = m_head;
		m_head = chunk;
		++m_chunkCount;
		for (std::size_t i = 0; i < SlotsPerChunk; ++i)
		{
			chunk->Slots[i].NextFree = m_freeHead;
			m_freeHead = &chunk->Slots[i];
		}
		return true;
	}

	// Clear 후 비점유 슬롯 전체를 free list 로 재구성.
	void RebuildFreeList()
	{
		m_freeHead = nullptr;
		for (Chunk* chunk = m_head; nullptr != chunk; chunk = chunk->Next)
		{
			for (std::size_t i = 0; i < SlotsPerChunk; ++i)
			{
				Slot& slot = chunk->Slots[i];
				slot.NextFree = m_freeHead;
				m_freeHead = &slot;
			}
		}
	}

	void FreeChunks()
	{
		Chunk* chunk = m_head;
		while (nullptr != chunk)
		{
			Chunk* next = chunk->Next;
			delete chunk;
			chunk = next;
		}
		m_head = nullptr;
		m_freeHead = nullptr;
		m_chunkCount = 0;
		m_liveObjects.clear();
		// 캐시 블록은 청크가 아니라 개별 heap 객체다 — 청크를 놓을 때 함께 반납한다.
		FreeCachedControlBlocks();
	}

private:
	Chunk*      m_head      = nullptr;
	Slot*       m_freeHead  = nullptr;
	std::size_t m_chunkCount = 0;
	std::vector<T*> m_liveObjects;
	// 참조가 0 인 채로 파괴된 객체의 ControlBlock — 다음 Allocate 가 되살려 쓴다.
	std::vector<SafePtrDetail::ControlBlock*> m_freeBlocks;
};
