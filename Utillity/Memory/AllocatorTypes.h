#pragma once

#include <cstddef>

struct AllocatorStats
{
	std::size_t TotalAllocatedBytes = 0;
	std::size_t ActiveAllocatedBytes = 0;
	std::size_t AllocationCount = 0;
	std::size_t FreeCount = 0;
};

