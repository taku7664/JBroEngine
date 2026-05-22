#include "pch.h"
#include "MallocAllocator.h"

#include <cstdlib>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

void* CMallocAllocator::Allocate(std::size_t size, std::size_t alignment)
{
	const std::size_t effectiveSize = std::max<std::size_t>(size, 1);
	const std::size_t effectiveAlignment = std::max<std::size_t>(alignment, alignof(void*));

#if defined(_MSC_VER)
	void* ptr = _aligned_malloc(effectiveSize, effectiveAlignment);
#else
	const std::size_t remainder = effectiveSize % effectiveAlignment;
	const std::size_t alignedSize = 0 == remainder ? effectiveSize : effectiveSize + (effectiveAlignment - remainder);
	void* ptr = std::aligned_alloc(effectiveAlignment, alignedSize);
#endif

	if (ptr)
	{
		m_stats.TotalAllocatedBytes += effectiveSize;
		m_stats.ActiveAllocatedBytes += effectiveSize;
		++m_stats.AllocationCount;
	}

	return ptr;
}

void CMallocAllocator::Free(void* ptr, std::size_t size)
{
	if (nullptr == ptr)
	{
		return;
	}

#if defined(_MSC_VER)
	_aligned_free(ptr);
#else
	std::free(ptr);
#endif

	if (size <= m_stats.ActiveAllocatedBytes)
	{
		m_stats.ActiveAllocatedBytes -= size;
	}
	else
	{
		m_stats.ActiveAllocatedBytes = 0;
	}
	++m_stats.FreeCount;
}

const AllocatorStats& CMallocAllocator::GetStats() const
{
	return m_stats;
}
