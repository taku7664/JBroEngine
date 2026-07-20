#pragma once

#include <cstddef>
#include <new>

enum class EMemoryTag : unsigned char
{
	Unknown,
	Array,
	Table,
	String,
	Reflection
};

struct HeapAllocator
{
	static void* Allocate(
		std::size_t size,
		std::size_t alignment,
		EMemoryTag tag = EMemoryTag::Unknown)
	{
		(void)tag;
		if (0 == size)
		{
			return nullptr;
		}

		if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
		{
			return ::operator new(size, std::align_val_t(alignment));
		}

		return ::operator new(size);
	}

	static void Deallocate(
		void* memory,
		std::size_t size,
		std::size_t alignment,
		EMemoryTag tag = EMemoryTag::Unknown) noexcept
	{
		(void)size;
		(void)tag;
		if (nullptr == memory)
		{
			return;
		}

		if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
		{
			::operator delete(memory, std::align_val_t(alignment));
			return;
		}

		::operator delete(memory);
	}
};
