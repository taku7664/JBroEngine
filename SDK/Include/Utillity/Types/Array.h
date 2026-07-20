#pragma once

#include "Utillity/Types/Allocator.h"
#include "Utillity/Types/ArrayView.h"
#include "Utillity/Types/TypeTraits.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

template<typename T, typename Allocator = HeapAllocator>
class Array
{
public:
	using ElementType = T;
	using SizeType = std::size_t;
	using DifferenceType = std::ptrdiff_t;
	using Iterator = T*;
	using ConstIterator = const T*;

	Array() noexcept = default;

	explicit Array(SizeType size)
	{
		Resize(size);
	}

	Array(std::initializer_list<T> values)
	{
		Append(values.begin(), values.size());
	}

	Array(const Array& other)
	{
		Append(other.Data(), other.Size());
	}

	Array(Array&& other) noexcept
		: m_data(other.m_data)
		, m_size(other.m_size)
		, m_capacity(other.m_capacity)
	{
		other.m_data = nullptr;
		other.m_size = 0;
		other.m_capacity = 0;
	}

	~Array()
	{
		Release();
	}

	Array& operator=(const Array& other)
	{
		if (this == &other)
		{
			return *this;
		}

		Array copy(other);
		Swap(copy);
		return *this;
	}

	Array& operator=(Array&& other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}

		Release();
		m_data = other.m_data;
		m_size = other.m_size;
		m_capacity = other.m_capacity;
		other.m_data = nullptr;
		other.m_size = 0;
		other.m_capacity = 0;
		return *this;
	}

	T* Data() noexcept
	{
		return m_data;
	}

	const T* Data() const noexcept
	{
		return m_data;
	}

	SizeType Size() const noexcept
	{
		return m_size;
	}

	SizeType Capacity() const noexcept
	{
		return m_capacity;
	}

	bool IsEmpty() const noexcept
	{
		return 0 == m_size;
	}

	T& operator[](SizeType index) noexcept
	{
		assert(index < m_size);
		return m_data[index];
	}

	const T& operator[](SizeType index) const noexcept
	{
		assert(index < m_size);
		return m_data[index];
	}

	T& First() noexcept
	{
		assert(0 != m_size);
		return m_data[0];
	}

	const T& First() const noexcept
	{
		assert(0 != m_size);
		return m_data[0];
	}

	T& Last() noexcept
	{
		assert(0 != m_size);
		return m_data[m_size - 1];
	}

	const T& Last() const noexcept
	{
		assert(0 != m_size);
		return m_data[m_size - 1];
	}

	Iterator begin() noexcept
	{
		return m_data;
	}

	ConstIterator begin() const noexcept
	{
		return m_data;
	}

	Iterator end() noexcept
	{
		return m_data + m_size;
	}

	ConstIterator end() const noexcept
	{
		return m_data + m_size;
	}

	ArrayView<T> View() noexcept
	{
		return ArrayView<T>(m_data, m_size);
	}

	ArrayView<const T> View() const noexcept
	{
		return ArrayView<const T>(m_data, m_size);
	}

	void Reserve(SizeType requestedCapacity)
	{
		if (requestedCapacity <= m_capacity)
		{
			return;
		}

		Reallocate(requestedCapacity);
	}

	void Resize(SizeType newSize)
	{
		if (newSize < m_size)
		{
			std::destroy(m_data + newSize, m_data + m_size);
			m_size = newSize;
			return;
		}

		if (newSize == m_size)
		{
			return;
		}

		Reserve(newSize);
		const SizeType oldSize = m_size;
		try
		{
			std::uninitialized_value_construct(m_data + oldSize, m_data + newSize);
			m_size = newSize;
		}
		catch (...)
		{
			std::destroy(m_data + oldSize, m_data + m_size);
			throw;
		}
	}

	void Clear() noexcept
	{
		std::destroy(m_data, m_data + m_size);
		m_size = 0;
	}

	void Reset(SizeType expectedCapacity = 0)
	{
		Clear();
		if (expectedCapacity > m_capacity)
		{
			Reserve(expectedCapacity);
		}
	}

	void Shrink()
	{
		if (m_size == m_capacity)
		{
			return;
		}

		if (0 == m_size)
		{
			ReleaseStorage();
			return;
		}

		Reallocate(m_size);
	}

	template<typename... Args>
	T& Emplace(Args&&... args)
	{
		if (m_size == m_capacity)
		{
			EnsureAdditionalCapacity(1);
		}
		T* element = std::construct_at(m_data + m_size, std::forward<Args>(args)...);
		++m_size;
		return *element;
	}

	T& Add(const T& value)
	{
		return Emplace(value);
	}

	T& Add(T&& value)
	{
		return Emplace(std::move(value));
	}

	template<typename... Args>
	T& EmplaceAt(SizeType index, Args&&... args)
	{
		assert(index <= m_size);
		if (index == m_size)
		{
			return Emplace(std::forward<Args>(args)...);
		}

		T value(std::forward<Args>(args)...);
		if (m_size == m_capacity)
		{
			EnsureAdditionalCapacity(1);
		}

		if constexpr (IsTriviallyRelocatableV<T>)
		{
			std::memmove(
				m_data + index + 1,
				m_data + index,
				(m_size - index) * sizeof(T));
			m_data[index] = std::move(value);
		}
		else
		{
			std::construct_at(m_data + m_size, std::move(m_data[m_size - 1]));
			std::move_backward(m_data + index, m_data + m_size - 1, m_data + m_size);
			m_data[index] = std::move(value);
		}

		++m_size;
		return m_data[index];
	}

	T& Insert(SizeType index, const T& value)
	{
		return EmplaceAt(index, value);
	}

	T& Insert(SizeType index, T&& value)
	{
		return EmplaceAt(index, std::move(value));
	}

	void Append(const T* values, SizeType count)
	{
		if (0 == count)
		{
			return;
		}

		const std::uintptr_t sourceAddress = reinterpret_cast<std::uintptr_t>(values);
		const std::uintptr_t beginAddress = reinterpret_cast<std::uintptr_t>(m_data);
		const std::uintptr_t endAddress = beginAddress + m_size * sizeof(T);
		const bool overlaps = sourceAddress >= beginAddress && sourceAddress < endAddress;
		if (overlaps)
		{
			Array copy;
			copy.Append(values, count);
			Append(copy.Data(), copy.Size());
			return;
		}

		EnsureAdditionalCapacity(count);
		std::uninitialized_copy_n(values, count, m_data + m_size);
		m_size += count;
	}

	void RemoveAt(SizeType index)
	{
		assert(index < m_size);
		const SizeType moveCount = m_size - index - 1;
		if constexpr (IsTriviallyRelocatableV<T>)
		{
			if (0 != moveCount)
			{
				std::memmove(m_data + index, m_data + index + 1, moveCount * sizeof(T));
			}
		}
		else
		{
			for (SizeType moveIndex = index; moveIndex + 1 < m_size; ++moveIndex)
			{
				m_data[moveIndex] = std::move(m_data[moveIndex + 1]);
			}
			std::destroy_at(m_data + m_size - 1);
		}
		--m_size;
	}

	template<typename Predicate>
	SizeType RemoveAll(Predicate&& predicate)
	{
		T* const newEnd = std::remove_if(
			m_data,
			m_data + m_size,
			std::forward<Predicate>(predicate));
		const SizeType newSize = static_cast<SizeType>(newEnd - m_data);
		const SizeType removed = m_size - newSize;
		std::destroy(newEnd, m_data + m_size);
		m_size = newSize;
		return removed;
	}

	template<typename Predicate>
	SizeType RemoveAllSwap(Predicate&& predicate)
	{
		const SizeType originalSize = m_size;
		SizeType index = 0;
		while (index < m_size)
		{
			if (predicate(m_data[index]))
			{
				RemoveAtSwap(index);
				continue;
			}
			++index;
		}
		return originalSize - m_size;
	}

	void RemoveAtSwap(SizeType index)
	{
		assert(index < m_size);
		const SizeType lastIndex = m_size - 1;
		if (index != lastIndex)
		{
			m_data[index] = std::move(m_data[lastIndex]);
		}
		std::destroy_at(m_data + lastIndex);
		--m_size;
	}

	T Pop()
	{
		assert(0 != m_size);
		T result = std::move(m_data[m_size - 1]);
		std::destroy_at(m_data + m_size - 1);
		--m_size;
		return result;
	}

	void Swap(Array& other) noexcept
	{
		using std::swap;
		swap(m_data, other.m_data);
		swap(m_size, other.m_size);
		swap(m_capacity, other.m_capacity);
	}

private:
	static constexpr SizeType MaxSize = std::numeric_limits<SizeType>::max() / sizeof(T);

	void EnsureAdditionalCapacity(SizeType additional)
	{
		if (additional > MaxSize - m_size)
		{
			throw std::bad_array_new_length();
		}

		const SizeType required = m_size + additional;
		if (required <= m_capacity)
		{
			return;
		}

		SizeType grown = m_capacity < 4 ? 4 : m_capacity + m_capacity / 2;
		if (grown < required || grown > MaxSize)
		{
			grown = required;
		}
		Reserve(grown);
	}

	void Reallocate(SizeType newCapacity)
	{
		if (newCapacity > MaxSize)
		{
			throw std::bad_array_new_length();
		}

		T* newData = static_cast<T*>(Allocator::Allocate(
			newCapacity * sizeof(T), alignof(T), EMemoryTag::Array));

		if constexpr (IsTriviallyRelocatableV<T>)
		{
			if (0 != m_size)
			{
				std::memcpy(newData, m_data, m_size * sizeof(T));
			}
		}
		else
		{
			try
			{
				if constexpr (
					std::is_nothrow_move_constructible_v<T> ||
					false == std::is_copy_constructible_v<T>)
				{
					std::uninitialized_move(m_data, m_data + m_size, newData);
				}
				else
				{
					std::uninitialized_copy(m_data, m_data + m_size, newData);
				}
			}
			catch (...)
			{
				Allocator::Deallocate(
					newData, newCapacity * sizeof(T), alignof(T), EMemoryTag::Array);
				throw;
			}
			std::destroy(m_data, m_data + m_size);
		}

		Allocator::Deallocate(
			m_data, m_capacity * sizeof(T), alignof(T), EMemoryTag::Array);
		m_data = newData;
		m_capacity = newCapacity;
	}

	void ReleaseStorage() noexcept
	{
		Allocator::Deallocate(
			m_data, m_capacity * sizeof(T), alignof(T), EMemoryTag::Array);
		m_data = nullptr;
		m_capacity = 0;
	}

	void Release() noexcept
	{
		Clear();
		ReleaseStorage();
	}

	T* m_data = nullptr;
	SizeType m_size = 0;
	SizeType m_capacity = 0;
};
