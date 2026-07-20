#pragma once

#include <type_traits>

template<typename T>
struct IsTriviallyRelocatable : std::bool_constant<std::is_trivially_copyable_v<T>>
{
};

template<typename T>
inline constexpr bool IsTriviallyRelocatableV = IsTriviallyRelocatable<T>::value;

#define JBRO_DECLARE_TRIVIALLY_RELOCATABLE(Type) \
	template<>                                      \
	struct IsTriviallyRelocatable<Type> : std::true_type
