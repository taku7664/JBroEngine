#pragma once

#include "GameFramework/Reflection/ReflectionTypes.h"
#include "Utillity/Types/Array.h"
#include "Utillity/Types/Table.h"

#include <new>
#include <type_traits>

template<typename T, EReflectPropertyType Type>
const ReflectTypeDesc& GetScalarReflectTypeDesc()
{
	static const ReflectTypeDesc descriptor = {
		Type,
		sizeof(T),
		alignof(T),
		std::is_trivially_copyable_v<T>
	};
	return descriptor;
}

template<typename T, typename Allocator>
const ReflectArrayOps& GetReflectArrayOps()
{
	static const ReflectArrayOps operations = {
		[](const void* array) -> std::size_t
		{
			return static_cast<const Array<T, Allocator>*>(array)->Size();
		},
		[](const void* array) -> std::size_t
		{
			return static_cast<const Array<T, Allocator>*>(array)->Capacity();
		},
		[](void* array, std::size_t index) -> void*
		{
			Array<T, Allocator>& values = *static_cast<Array<T, Allocator>*>(array);
			return index < values.Size() ? &values[index] : nullptr;
		},
		[](const void* array, std::size_t index) -> const void*
		{
			const Array<T, Allocator>& values = *static_cast<const Array<T, Allocator>*>(array);
			return index < values.Size() ? &values[index] : nullptr;
		},
		[](void* array)
		{
			static_assert(std::is_default_constructible_v<T>);
			static_cast<Array<T, Allocator>*>(array)->Emplace();
		},
		[](void* array, std::size_t index)
		{
			Array<T, Allocator>& values = *static_cast<Array<T, Allocator>*>(array);
			if (index < values.Size())
			{
				values.RemoveAt(index);
			}
		},
		[](void* array, std::size_t fromIndex, std::size_t toIndex)
		{
			Array<T, Allocator>& values = *static_cast<Array<T, Allocator>*>(array);
			if (fromIndex >= values.Size() || toIndex >= values.Size() || fromIndex == toIndex)
			{
				return;
			}
			T moved = std::move(values[fromIndex]);
			values.RemoveAt(fromIndex);
			values.Insert(toIndex, std::move(moved));
		},
		[](void* array)
		{
			static_cast<Array<T, Allocator>*>(array)->Clear();
		}
	};

	return operations;
}

template<
	typename T,
	EReflectPropertyType ElementType,
	typename Allocator = HeapAllocator>
const ReflectTypeDesc& GetArrayReflectTypeDesc()
{
	static const ReflectTypeDesc descriptor = []
	{
		ReflectTypeDesc value;
		value.Type = EReflectPropertyType::Array;
		value.Size = sizeof(Array<T, Allocator>);
		value.Alignment = alignof(Array<T, Allocator>);
		value.IsTriviallyCopyable = false;
		value.Element = &GetScalarReflectTypeDesc<T, ElementType>();
		value.ArrayOps = &GetReflectArrayOps<T, Allocator>();
		return value;
	}();

	return descriptor;
}

template<typename Key, typename Value, typename Allocator>
const ReflectTableOps& GetReflectTableOps()
{
	using TableType = Table<Key, Value, Hash<Key>, EqualTo<>, Allocator>;

	// 슬롯 커서를 앞으로 밀어 유효 슬롯을 찾는다. 없으면 InvalidTableSlot.
	// 인스펙터가 프레임마다 도는 경로라 Capacity() 를 한 번만 읽는다.
	static const auto advance = [](const TableType& table, std::size_t slot) -> std::size_t
	{
		const std::size_t capacity = table.Capacity();
		for (; slot < capacity; ++slot)
		{
			if (table.IsSlotOccupied(slot))
			{
				return slot;
			}
		}
		return InvalidTableSlot;
	};

	static const ReflectTableOps operations = {
		[](const void* table) -> std::size_t
		{
			return static_cast<const TableType*>(table)->Size();
		},
		[](const void* table) -> std::size_t
		{
			return advance(*static_cast<const TableType*>(table), 0);
		},
		[](const void* table, std::size_t slot) -> std::size_t
		{
			// slot 이 이미 끝이면 더 밀지 않는다(InvalidTableSlot + 1 = 0 으로 감싸 도는 것 방지).
			if (InvalidTableSlot == slot)
			{
				return InvalidTableSlot;
			}
			return advance(*static_cast<const TableType*>(table), slot + 1);
		},
		[](const void* table, std::size_t slot) -> const void*
		{
			const TableType& values = *static_cast<const TableType*>(table);
			return values.IsSlotOccupied(slot) ? &values.KeyAt(slot) : nullptr;
		},
		[](void* table, std::size_t slot) -> void*
		{
			TableType& values = *static_cast<TableType*>(table);
			return values.IsSlotOccupied(slot) ? &values.ValueAt(slot) : nullptr;
		},
		[](const void* table, std::size_t slot) -> const void*
		{
			const TableType& values = *static_cast<const TableType*>(table);
			return values.IsSlotOccupied(slot) ? &values.ValueAt(slot) : nullptr;
		},
		[](const void* table, const void* key) -> bool
		{
			return static_cast<const TableType*>(table)->Contains(*static_cast<const Key*>(key));
		},
		[](void* table, const void* key) -> bool
		{
			static_assert(std::is_default_constructible_v<Value>);
			return static_cast<TableType*>(table)->TryAdd(*static_cast<const Key*>(key), Value{});
		},
		[](void* table, const void* key) -> bool
		{
			return static_cast<TableType*>(table)->Remove(*static_cast<const Key*>(key));
		},
		[](void* table, const void* key) -> void*
		{
			return static_cast<TableType*>(table)->Find(*static_cast<const Key*>(key));
		},
		[]() -> void*
		{
			// Table 과 같은 얼로케이터로 잡는다 — 호스트가 만든 키를 게임 DLL 이 돌려줘도
			// (또는 그 반대) 같은 힙으로 돌아가야 한다.
			void* storage = Allocator::Allocate(sizeof(Key), alignof(Key), EMemoryTag::Unknown);
			return nullptr == storage ? nullptr : new (storage) Key();
		},
		[](void* key)
		{
			if (nullptr == key)
			{
				return;
			}
			static_cast<Key*>(key)->~Key();
			Allocator::Deallocate(key, sizeof(Key), alignof(Key), EMemoryTag::Unknown);
		},
		[]() -> void*
		{
			static_assert(std::is_default_constructible_v<Value>);
			void* storage = Allocator::Allocate(sizeof(Value), alignof(Value), EMemoryTag::Unknown);
			return nullptr == storage ? nullptr : new (storage) Value();
		},
		[](void* value)
		{
			if (nullptr == value)
			{
				return;
			}
			static_cast<Value*>(value)->~Value();
			Allocator::Deallocate(value, sizeof(Value), alignof(Value), EMemoryTag::Unknown);
		},
		[](void* destination, const void* source)
		{
			*static_cast<Key*>(destination) = *static_cast<const Key*>(source);
		},
		[](void* destination, const void* source)
		{
			*static_cast<Value*>(destination) = *static_cast<const Value*>(source);
		},
		[](void* table)
		{
			static_cast<TableType*>(table)->Clear();
		}
	};

	return operations;
}

template<
	typename Key,
	EReflectPropertyType KeyType,
	typename Value,
	EReflectPropertyType ValueType,
	typename Allocator = HeapAllocator>
const ReflectTypeDesc& GetTableReflectTypeDesc()
{
	using TableType = Table<Key, Value, Hash<Key>, EqualTo<>, Allocator>;

	static const ReflectTypeDesc descriptor = []
	{
		ReflectTypeDesc value;
		value.Type = EReflectPropertyType::Table;
		value.Size = sizeof(TableType);
		value.Alignment = alignof(TableType);
		value.IsTriviallyCopyable = false;
		value.Key = &GetScalarReflectTypeDesc<Key, KeyType>();
		value.Value = &GetScalarReflectTypeDesc<Value, ValueType>();
		value.TableOps = &GetReflectTableOps<Key, Value, Allocator>();
		return value;
	}();

	return descriptor;
}
