#pragma once

#include "AllocatorTypes.h"

#include <cstddef>

class IAllocator
{
public:
	virtual ~IAllocator() = default;

public:
	virtual void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) = 0;
	virtual void Free(void* ptr, std::size_t size = 0) = 0;
	virtual const AllocatorStats& GetStats() const = 0;
};

