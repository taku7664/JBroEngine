#include <JBro/Types/LinearAllocator.h>

#include <algorithm>
#include <new>

namespace JBro
{
	LinearAllocator::~LinearAllocator()
	{
		Shutdown();
	}

	bool LinearAllocator::Initialize(std::size_t capacityBytes)
	{
		if (m_base != nullptr || capacityBytes == 0)
		{
			return false;
		}
		void* block = HeapAllocator{}.Allocate(capacityBytes, alignof(std::max_align_t));
		if (block == nullptr)
		{
			return false;
		}
		m_base = static_cast<std::byte*>(block);
		m_capacity = capacityBytes;
		m_used = 0;
		m_peakUsed = 0;
		m_requestCount = 0;
		m_overflowCount = 0;
		return true;
	}

	void LinearAllocator::Shutdown()
	{
		ReleaseOverflow();
		if (m_base == nullptr)
		{
			return;
		}
		HeapAllocator{}.Deallocate(m_base, m_capacity, alignof(std::max_align_t));
		m_base = nullptr;
		m_capacity = 0;
		m_used = 0;
	}

	void LinearAllocator::Reset()
	{
		m_used = 0;
		ReleaseOverflow();
	}

	JAllocator LinearAllocator::GetInterface()
	{
		JAllocator result;
		result.userData = this;
		result.allocate = &AllocateThunk;
		result.free = &FreeThunk;
		// 되감기가 해제를 대신하므로 재할당 경로를 두지 않는다.
		result.reallocate = nullptr;
		return result;
	}

	bool LinearAllocator::IsInitialized() const
	{
		return m_base != nullptr;
	}

	std::size_t LinearAllocator::GetCapacity() const
	{
		return m_capacity;
	}

	std::size_t LinearAllocator::GetUsedBytes() const
	{
		return m_used;
	}

	std::size_t LinearAllocator::GetRequestCount() const
	{
		return m_requestCount;
	}

	std::size_t LinearAllocator::GetOverflowCount() const
	{
		return m_overflowCount;
	}

	std::size_t LinearAllocator::GetPeakUsedBytes() const
	{
		return m_peakUsed;
	}

	void* LinearAllocator::AllocateThunk(void* userData, std::size_t size, std::size_t alignment)
	{
		return static_cast<LinearAllocator*>(userData)->Allocate(size, alignment);
	}

	void LinearAllocator::FreeThunk(void*, void*)
	{
		// 선형 할당기는 개별 해제가 없다. 아레나 안이든 넘친 블록이든 Reset 이 거둬 간다.
	}

	void* LinearAllocator::Allocate(std::size_t size, std::size_t alignment)
	{
		if (size == 0)
		{
			return nullptr;
		}
		++m_requestCount;

		const std::size_t effectiveAlignment = (std::max)(alignment, alignof(void*));
		if (m_base != nullptr)
		{
			const auto current = reinterpret_cast<std::uintptr_t>(m_base + m_used);
			const std::size_t misalignment = static_cast<std::size_t>(current % effectiveAlignment);
			const std::size_t padding = misalignment == 0 ? 0 : effectiveAlignment - misalignment;
			// 넘침 검사를 뺄셈으로 한다. padding + size 가 되감기면 안 된다.
			const std::size_t remaining = m_capacity - m_used;
			if (padding <= remaining && size <= remaining - padding)
			{
				std::byte* result = m_base + m_used + padding;
				m_used += padding + size;
				m_peakUsed = (std::max)(m_peakUsed, m_used);
				return result;
			}
		}

		void* block = HeapAllocator{}.Allocate(size, effectiveAlignment);
		if (block == nullptr)
		{
			return nullptr;
		}
		OverflowBlock record;
		record.memory = block;
		record.size = size;
		record.alignment = effectiveAlignment;
		try
		{
			m_overflow.Add(record);
		}
		catch (...)
		{
			// 기록하지 못하면 되감을 때 거둘 수 없다. 새게 두느니 지금 돌려준다.
			HeapAllocator{}.Deallocate(block, size, effectiveAlignment);
			return nullptr;
		}
		++m_overflowCount;
		return block;
	}

	void LinearAllocator::ReleaseOverflow()
	{
		for (const OverflowBlock& block : m_overflow)
		{
			HeapAllocator{}.Deallocate(block.memory, block.size, block.alignment);
		}
		m_overflow.Clear();
	}
}
