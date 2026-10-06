#pragma once

#include <functional>
#include <type_traits>

namespace JBro
{
class Bool
{
public:
	constexpr Bool() noexcept = default;
	constexpr Bool(bool value) noexcept : Value(value) {}

	constexpr operator bool() const noexcept { return Value; }
	constexpr bool Get() const noexcept { return Value; }
	constexpr void Set(bool value) noexcept { Value = value; }
	constexpr bool IsTrue() const noexcept { return Value; }
	constexpr bool IsFalse() const noexcept { return !Value; }

	constexpr Bool& operator=(bool value) noexcept
	{
		Value = value;
		return *this;
	}

	// 필드를 엔진 타입으로 옮긴 뒤에도 `dirty |= changed` 가 그대로 읽혀야 한다(D-290).
	constexpr Bool& operator|=(bool rhs) noexcept { Value = Value || rhs; return *this; }
	constexpr Bool& operator&=(bool rhs) noexcept { Value = Value && rhs; return *this; }
	constexpr Bool& operator^=(bool rhs) noexcept { Value = Value != rhs; return *this; }

	constexpr Bool& Toggle() noexcept
	{
		Value = !Value;
		return *this;
	}

	bool Value = false;
};

static_assert(sizeof(Bool) == sizeof(bool));
static_assert(alignof(Bool) == alignof(bool));
static_assert(std::is_trivially_copyable_v<Bool>);
static_assert(std::is_standard_layout_v<Bool>);
}

namespace std
{
	template <>
	struct hash<JBro::Bool>
	{
		std::size_t operator()(JBro::Bool value) const noexcept
		{
			return hash<bool>()(value.Get());
		}
	};
}
