#pragma once

#include "Utillity/Types/IntegerType.h"

#include <cstdint>
#include <type_traits>

// 부호 없는 정수 강타입. 본체는 IntegerType<T> 하나이고 여기선 이름만 붙인다.
// 규칙은 Int.h 와 같다 — 실체는 UInt64 / UInt32, `UInt` 는 UInt64 의 별칭.
using UInt64 = IntegerType<std::uint64_t>;
using UInt32 = IntegerType<std::uint32_t>;
using UInt   = UInt64;

static_assert(sizeof(UInt64) == sizeof(std::uint64_t));
static_assert(alignof(UInt64) == alignof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<UInt64>);
static_assert(std::is_standard_layout_v<UInt64>);

static_assert(sizeof(UInt32) == sizeof(std::uint32_t));
static_assert(alignof(UInt32) == alignof(std::uint32_t));
static_assert(std::is_trivially_copyable_v<UInt32>);
static_assert(std::is_standard_layout_v<UInt32>);
