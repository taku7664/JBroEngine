#pragma once

#include "IAllocator.h"

class CMallocAllocator final : public IAllocator
{
public:
	void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) override;
	void Free(void* ptr, std::size_t size = 0) override;
	const AllocatorStats& GetStats() const override;

private:
	AllocatorStats m_stats;
};

