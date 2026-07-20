#pragma once

#include "GameFramework/Reflection/ReflectionTypes.h"
#include "Utillity/Types/Array.h"

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
