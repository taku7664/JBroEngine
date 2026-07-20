#pragma once

#include "Utillity/Types/Allocator.h"
#include "Utillity/Types/Hash.h"
#include "Utillity/Types/Simd128.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

template<
	typename Key,
	typename Value,
	typename Hasher = Hash<Key>,
	typename KeyEqual = EqualTo<>,
	typename Allocator = HeapAllocator>
class Table
{
private:
	static constexpr std::int8_t EmptyControl = static_cast<std::int8_t>(0x80);
	static constexpr std::int8_t DeletedControl = static_cast<std::int8_t>(0xFE);
	static constexpr std::size_t GroupWidth = Simd128::ByteWidth;
	static constexpr std::size_t MinimumCapacity = GroupWidth;

	struct Entry
	{
		Key KeyValue;
		Value MappedValue;
	};

	using EntryStorage = std::aligned_storage_t<sizeof(Entry), alignof(Entry)>;

public:
	using KeyType = Key;
	using MappedType = Value;
	using SizeType = std::size_t;

	class Iterator
	{
	public:
		Iterator() noexcept = default;

		Entry& operator*() const noexcept
		{
			return m_table->EntryAt(m_index);
		}

		Entry* operator->() const noexcept
		{
			return &m_table->EntryAt(m_index);
		}

		Iterator& operator++() noexcept
		{
			++m_index;
			SkipEmpty();
			return *this;
		}

		bool operator==(const Iterator& other) const noexcept
		{
			return m_table == other.m_table && m_index == other.m_index;
		}

		bool operator!=(const Iterator& other) const noexcept
		{
			return false == (*this == other);
		}

	private:
		friend class Table;

		Iterator(Table* table, SizeType index) noexcept
			: m_table(table)
			, m_index(index)
		{
			SkipEmpty();
		}

		void SkipEmpty() noexcept
		{
			while (nullptr != m_table && m_index < m_table->m_capacity)
			{
				if (m_table->IsFull(m_table->m_controls[m_index]))
				{
					break;
				}
				++m_index;
			}
		}

		Table* m_table = nullptr;
		SizeType m_index = 0;
	};

	class ConstIterator
	{
	public:
		ConstIterator() noexcept = default;

		const Entry& operator*() const noexcept
		{
			return m_table->EntryAt(m_index);
		}

		const Entry* operator->() const noexcept
		{
			return &m_table->EntryAt(m_index);
		}

		ConstIterator& operator++() noexcept
		{
			++m_index;
			SkipEmpty();
			return *this;
		}

		bool operator==(const ConstIterator& other) const noexcept
		{
			return m_table == other.m_table && m_index == other.m_index;
		}

		bool operator!=(const ConstIterator& other) const noexcept
		{
			return false == (*this == other);
		}

	private:
		friend class Table;

		ConstIterator(const Table* table, SizeType index) noexcept
			: m_table(table)
			, m_index(index)
		{
			SkipEmpty();
		}

		void SkipEmpty() noexcept
		{
			while (nullptr != m_table && m_index < m_table->m_capacity)
			{
				if (m_table->IsFull(m_table->m_controls[m_index]))
				{
					break;
				}
				++m_index;
			}
		}

		const Table* m_table = nullptr;
		SizeType m_index = 0;
	};

	Table() noexcept = default;

	Table(const Table& other)
	{
		Reserve(other.Size());
		for (const Entry& entry : other)
		{
			TryAdd(entry.KeyValue, entry.MappedValue);
		}
	}

	Table(Table&& other) noexcept
		: m_controls(other.m_controls)
		, m_entries(other.m_entries)
		, m_size(other.m_size)
		, m_deleted(other.m_deleted)
		, m_capacity(other.m_capacity)
		, m_hasher(std::move(other.m_hasher))
		, m_equal(std::move(other.m_equal))
	{
		other.m_controls = nullptr;
		other.m_entries = nullptr;
		other.m_size = 0;
		other.m_deleted = 0;
		other.m_capacity = 0;
	}

	~Table()
	{
		Release();
	}

	Table& operator=(const Table& other)
	{
		if (this == &other)
		{
			return *this;
		}

		Table copy(other);
		Swap(copy);
		return *this;
	}

	Table& operator=(Table&& other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}

		Release();
		m_controls = other.m_controls;
		m_entries = other.m_entries;
		m_size = other.m_size;
		m_deleted = other.m_deleted;
		m_capacity = other.m_capacity;
		m_hasher = std::move(other.m_hasher);
		m_equal = std::move(other.m_equal);
		other.m_controls = nullptr;
		other.m_entries = nullptr;
		other.m_size = 0;
		other.m_deleted = 0;
		other.m_capacity = 0;
		return *this;
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

	template<typename LookupKey>
	Value* Find(const LookupKey& key)
	{
		const SizeType index = FindIndex(key);
		return index == m_capacity ? nullptr : &EntryAt(index).MappedValue;
	}

	template<typename LookupKey>
	const Value* Find(const LookupKey& key) const
	{
		const SizeType index = FindIndex(key);
		return index == m_capacity ? nullptr : &EntryAt(index).MappedValue;
	}

	template<typename LookupKey>
	bool Contains(const LookupKey& key) const
	{
		return nullptr != Find(key);
	}

	template<typename KeyArg, typename ValueArg>
	bool TryAdd(KeyArg&& key, ValueArg&& value)
	{
		EnsureInsertCapacity();
		const std::size_t hash = m_hasher(key);
		const FindResult result = FindInsertIndex(key, hash);
		if (result.Found)
		{
			return false;
		}

		const bool reusedDeleted = DeletedControl == m_controls[result.Index];
		std::construct_at(
			EntryPointer(result.Index),
			Entry{ std::forward<KeyArg>(key), std::forward<ValueArg>(value) });
		m_controls[result.Index] = HashFragment(hash);
		++m_size;
		if (reusedDeleted)
		{
			--m_deleted;
		}
		return true;
	}

	template<typename KeyArg, typename ValueArg>
	Value& InsertOrAssign(KeyArg&& key, ValueArg&& value)
	{
		EnsureInsertCapacity();
		const std::size_t hash = m_hasher(key);
		const FindResult result = FindInsertIndex(key, hash);
		if (result.Found)
		{
			EntryAt(result.Index).MappedValue = std::forward<ValueArg>(value);
			return EntryAt(result.Index).MappedValue;
		}

		const bool reusedDeleted = DeletedControl == m_controls[result.Index];
		Entry* entry = std::construct_at(
			EntryPointer(result.Index),
			Entry{ std::forward<KeyArg>(key), std::forward<ValueArg>(value) });
		m_controls[result.Index] = HashFragment(hash);
		++m_size;
		if (reusedDeleted)
		{
			--m_deleted;
		}
		return entry->MappedValue;
	}

	template<typename LookupKey>
	bool Remove(const LookupKey& key)
	{
		const SizeType index = FindIndex(key);
		if (index == m_capacity)
		{
			return false;
		}

		std::destroy_at(EntryPointer(index));
		m_controls[index] = DeletedControl;
		--m_size;
		++m_deleted;
		return true;
	}

	void Reserve(SizeType expectedSize)
	{
		if (expectedSize <= MaxSizeForCapacity(m_capacity))
		{
			return;
		}

		SizeType requiredCapacity = MinimumCapacity;
		while (MaxSizeForCapacity(requiredCapacity) < expectedSize)
		{
			if (requiredCapacity > std::numeric_limits<SizeType>::max() / 2)
			{
				throw std::bad_array_new_length();
			}
			requiredCapacity *= 2;
		}
		Rehash(requiredCapacity);
	}

	void Clear() noexcept
	{
		if (nullptr == m_controls)
		{
			return;
		}

		for (SizeType index = 0; index < m_capacity; ++index)
		{
			if (IsFull(m_controls[index]))
			{
				std::destroy_at(EntryPointer(index));
			}
		}
		std::memset(m_controls, EmptyControl, m_capacity);
		m_size = 0;
		m_deleted = 0;
	}

	void Reset(SizeType expectedSize = 0)
	{
		Clear();
		Reserve(expectedSize);
	}

	Iterator begin() noexcept
	{
		return Iterator(this, 0);
	}

	Iterator end() noexcept
	{
		return Iterator(this, m_capacity);
	}

	ConstIterator begin() const noexcept
	{
		return ConstIterator(this, 0);
	}

	ConstIterator end() const noexcept
	{
		return ConstIterator(this, m_capacity);
	}

	void Swap(Table& other) noexcept
	{
		using std::swap;
		swap(m_controls, other.m_controls);
		swap(m_entries, other.m_entries);
		swap(m_size, other.m_size);
		swap(m_deleted, other.m_deleted);
		swap(m_capacity, other.m_capacity);
		swap(m_hasher, other.m_hasher);
		swap(m_equal, other.m_equal);
	}

private:
	struct FindResult
	{
		SizeType Index = 0;
		bool Found = false;
	};

	static bool IsFull(std::int8_t control) noexcept
	{
		return control >= 0;
	}

	static std::int8_t HashFragment(std::size_t hash) noexcept
	{
		return static_cast<std::int8_t>(hash & 0x7F);
	}

	SizeType InitialGroup(std::size_t hash) const noexcept
	{
		const SizeType groupCount = m_capacity / GroupWidth;
		return ((hash >> 7) & (groupCount - 1)) * GroupWidth;
	}

	static SizeType MaxSizeForCapacity(SizeType capacity) noexcept
	{
		if (0 == capacity)
		{
			return 0;
		}

		return capacity - capacity / 8;
	}

	Entry* EntryPointer(SizeType index) noexcept
	{
		return std::launder(reinterpret_cast<Entry*>(m_entries + index));
	}

	const Entry* EntryPointer(SizeType index) const noexcept
	{
		return std::launder(reinterpret_cast<const Entry*>(m_entries + index));
	}

	Entry& EntryAt(SizeType index) noexcept
	{
		return *EntryPointer(index);
	}

	const Entry& EntryAt(SizeType index) const noexcept
	{
		return *EntryPointer(index);
	}

	template<typename LookupKey>
	SizeType FindIndex(const LookupKey& key) const
	{
		if (0 == m_capacity)
		{
			return 0;
		}

		const std::size_t hash = m_hasher(key);
		const std::int8_t fragment = HashFragment(hash);
		SizeType group = InitialGroup(hash);
		const SizeType startGroup = group;
		do
		{
			std::uint16_t candidates = Simd128::MatchBytes(m_controls + group, fragment);
			while (0 != candidates)
			{
				const unsigned lane = Simd128::FirstMatchIndex(candidates);
				const SizeType index = group + lane;
				if (m_equal(EntryAt(index).KeyValue, key))
				{
					return index;
				}
				candidates &= static_cast<std::uint16_t>(candidates - 1);
			}

			if (0 != Simd128::MatchBytes(m_controls + group, EmptyControl))
			{
				return m_capacity;
			}

			group = (group + GroupWidth) & (m_capacity - 1);
		}
		while (group != startGroup);

		return m_capacity;
	}

	template<typename LookupKey>
	FindResult FindInsertIndex(const LookupKey& key, std::size_t hash) const
	{
		const std::int8_t fragment = HashFragment(hash);
		SizeType group = InitialGroup(hash);
		const SizeType startGroup = group;
		SizeType firstDeleted = m_capacity;

		do
		{
			std::uint16_t candidates = Simd128::MatchBytes(m_controls + group, fragment);
			while (0 != candidates)
			{
				const unsigned lane = Simd128::FirstMatchIndex(candidates);
				const SizeType index = group + lane;
				if (m_equal(EntryAt(index).KeyValue, key))
				{
					return FindResult{ index, true };
				}
				candidates &= static_cast<std::uint16_t>(candidates - 1);
			}

			if (m_capacity == firstDeleted)
			{
				const std::uint16_t deleted = Simd128::MatchBytes(m_controls + group, DeletedControl);
				if (0 != deleted)
				{
					firstDeleted = group + Simd128::FirstMatchIndex(deleted);
				}
			}

			const std::uint16_t empty = Simd128::MatchBytes(m_controls + group, EmptyControl);
			if (0 != empty)
			{
				const SizeType emptyIndex = group + Simd128::FirstMatchIndex(empty);
				return FindResult{
					m_capacity == firstDeleted ? emptyIndex : firstDeleted,
					false
				};
			}

			group = (group + GroupWidth) & (m_capacity - 1);
		}
		while (group != startGroup);

		assert(m_capacity != firstDeleted);
		return FindResult{ firstDeleted, false };
	}

	void EnsureInsertCapacity()
	{
		if (0 == m_capacity)
		{
			Rehash(MinimumCapacity);
			return;
		}

		if (m_size + m_deleted + 1 > MaxSizeForCapacity(m_capacity))
		{
			if (m_deleted > m_size / 2)
			{
				Rehash(m_capacity);
			}
			else
			{
				Rehash(m_capacity * 2);
			}
		}
	}

	void Rehash(SizeType newCapacity)
	{
		Table replacement;
		replacement.AllocateStorage(newCapacity);
		for (SizeType index = 0; index < m_capacity; ++index)
		{
			if (IsFull(m_controls[index]))
			{
				Entry& entry = EntryAt(index);
				replacement.TryAdd(std::move(entry.KeyValue), std::move(entry.MappedValue));
			}
		}
		Swap(replacement);
	}

	void AllocateStorage(SizeType capacity)
	{
		assert(capacity >= MinimumCapacity);
		assert(0 == (capacity & (capacity - 1)));
		m_controls = static_cast<std::int8_t*>(Allocator::Allocate(
			capacity * sizeof(std::int8_t), alignof(std::int8_t), EMemoryTag::Table));
		try
		{
			m_entries = static_cast<EntryStorage*>(Allocator::Allocate(
				capacity * sizeof(EntryStorage), alignof(EntryStorage), EMemoryTag::Table));
		}
		catch (...)
		{
			Allocator::Deallocate(
				m_controls, capacity * sizeof(std::int8_t), alignof(std::int8_t), EMemoryTag::Table);
			m_controls = nullptr;
			throw;
		}

		std::memset(m_controls, EmptyControl, capacity);
		m_capacity = capacity;
	}

	void Release() noexcept
	{
		Clear();
		Allocator::Deallocate(
			m_controls, m_capacity * sizeof(std::int8_t), alignof(std::int8_t), EMemoryTag::Table);
		Allocator::Deallocate(
			m_entries, m_capacity * sizeof(EntryStorage), alignof(EntryStorage), EMemoryTag::Table);
		m_controls = nullptr;
		m_entries = nullptr;
		m_capacity = 0;
	}

	std::int8_t* m_controls = nullptr;
	EntryStorage* m_entries = nullptr;
	SizeType m_size = 0;
	SizeType m_deleted = 0;
	SizeType m_capacity = 0;
	[[no_unique_address]] Hasher m_hasher;
	[[no_unique_address]] KeyEqual m_equal;
};
