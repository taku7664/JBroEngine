#pragma once

#include "Utillity/Types/IntegerType.h"

#include <cstdint>
#include <type_traits>

// 부호 있는 정수 강타입. 본체는 IntegerType<T> 하나이고 여기선 이름만 붙인다.
//
// **실체는 폭이 붙은 Int64 / Int32 다.** `Int` 는 Int64 의 별칭으로 남겨 둔다 —
// 기존 코드와 스크립트가 전부 이 이름을 쓰고 있고, 폭을 밝히지 않은 정수는
// 64비트라는 규칙이 이미 서 있다.
using Int64 = IntegerType<std::int64_t>;
using Int32 = IntegerType<std::int32_t>;
using Int   = Int64;

static_assert(sizeof(Int64) == sizeof(std::int64_t));
static_assert(alignof(Int64) == alignof(std::int64_t));
static_assert(std::is_trivially_copyable_v<Int64>);
static_assert(std::is_standard_layout_v<Int64>);

static_assert(sizeof(Int32) == sizeof(std::int32_t));
static_assert(alignof(Int32) == alignof(std::int32_t));
static_assert(std::is_trivially_copyable_v<Int32>);
static_assert(std::is_standard_layout_v<Int32>);
