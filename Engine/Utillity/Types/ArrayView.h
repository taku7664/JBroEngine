#pragma once

#include <cstddef>
#include <type_traits>

template<typename T>
class ArrayView
{
public:
	using ElementType = T;
	using ValueType = std::remove_cv_t<T>;
	using SizeType = std::size_t;
	using Iterator = T*;
	using ConstIterator = const ValueType*;

	constexpr ArrayView() noexcept = default;

	constexpr ArrayView(T* data, SizeType size) noexcept
		: m_data(data)
		, m_size(size)
	{
	}

	template<std::size_t N>
	constexpr ArrayView(T (&values)[N]) noexcept
		: m_data(values)
		, m_size(N)
	{
	}

	template<typename U>
	requires std::is_convertible_v<U(*)[], T(*)[]>
	constexpr ArrayView(const ArrayView<U>& other) noexcept
		: m_data(other.Data())
		, m_size(other.Size())
	{
	}

	constexpr T* Data() const noexcept
	{
		return m_data;
	}

	constexpr SizeType Size() const noexcept
	{
		return m_size;
	}

	constexpr bool IsEmpty() const noexcept
	{
		return 0 == m_size;
	}

	constexpr T& operator[](SizeType index) const noexcept
	{
		return m_data[index];
	}

	constexpr Iterator begin() const noexcept
	{
		return m_data;
	}

	constexpr Iterator end() const noexcept
	{
		return m_data + m_size;
	}

private:
	T* m_data = nullptr;
	SizeType m_size = 0;
};
