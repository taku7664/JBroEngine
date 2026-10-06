#pragma once

#include <JBro/Reflection/ScalarCodec.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

#include <cstdint>

namespace JBro
{
    namespace Detail
    {
        template <typename>
        inline constexpr bool AlwaysFalse = false;
    }

    // 타입 하나에서 그 타입의 `TypeDescriptor` 로 가는 유일한 길이다.
    //
    // **특수화가 없는 타입을 필드로 쓰면 컴파일이 실패한다.** 기존 엔진은 모르는 타입을
    // 만나면 로그 경고를 남기고 그 필드를 조용히 빠뜨렸다 — 저장 파일에서 값이 사라지는데
    // 아무도 모르는 실패다. 여기서는 필드를 선언하는 순간 멈춘다.
    //
    // 새 타입을 지원하는 비용은 특수화 하나다. `Vector2` 처럼 구조를 가진 타입은 자기 모듈에서
    // 특수화한다 — Core 가 Framework 타입을 알 필요가 없다.
    template <typename T>
    struct TypeDescriptorOf
    {
        static_assert(Detail::AlwaysFalse<T>,
            "this field type has no TypeDescriptor - specialize TypeDescriptorOf<T> for it");
    };

    // 산술 타입 하나를 등록한다. 설명서는 처음 물어볼 때 한 번 만들어지고 그 뒤로 같은 주소다 —
    // `PropertyInfo::type` 이 그 주소를 들고 다니므로 매번 새로 만들면 안 된다.
    #define JBRO_DEFINE_SCALAR_TYPE(Type, TypeNameLiteral)                                  template <>                                                                         struct TypeDescriptorOf<Type>                                                       {                                                                                       static const TypeDescriptor& Get()                                                  {                                                                                       static const TypeDescriptor descriptor =                                                MakeScalarTypeDescriptor<Type>(TypeNameLiteral);                                return descriptor;                                                              }                                                                               }

    // 엔진 값 타입 하나를 등록한다(D-290). 이름과 코덱은 감싼 원시 타입의 것이다 - 값 타입은 원시 값 하나만 든
    // standard-layout 이라 객체와 첫 멤버가 포인터 상호 변환되고(크기·정렬은 각 타입 헤더가 static_assert 로 잠근다),
    // 그래서 저장 파일에 적히는 타입 이름(`float`·`int32`)과 글자는 원시 타입 시절과 같다.
    #define JBRO_DEFINE_VALUE_TYPE(Type, RawType, TypeNameLiteral)                          \
    template <>                                                                         \
    struct TypeDescriptorOf<Type>                                                       \
    {                                                                                   \
        static_assert(sizeof(Type) == sizeof(RawType) && alignof(Type) == alignof(RawType)); \
        static_assert(std::is_standard_layout_v<Type> && std::is_trivially_copyable_v<Type>); \
        static const TypeDescriptor& Get()                                              \
        {                                                                               \
            static const TypeDescriptor descriptor =                                    \
                MakeScalarTypeDescriptor<RawType>(TypeNameLiteral);                     \
            return descriptor;                                                          \
        }                                                                               \
    }

    // **필드는 엔진 값 타입으로 적는다.** `bool`·`float`·`std::int32_t`·`std::int64_t`·`std::uint32_t`·`std::uint64_t` 는
    // 일부러 등록하지 않는다 - 그런 필드를 선언하면 위의 static_assert 로 컴파일이 멈춘다(D-290).
    // 엔진 타입이 없는 폭(8·16 비트, `double`)만 원시 타입으로 남는다.
    JBRO_DEFINE_VALUE_TYPE(Bool,   bool,          "bool");
    JBRO_DEFINE_VALUE_TYPE(Int32,  std::int32_t,  "int32");
    JBRO_DEFINE_VALUE_TYPE(Int64,  std::int64_t,  "int64");
    JBRO_DEFINE_VALUE_TYPE(UInt32, std::uint32_t, "uint32");
    JBRO_DEFINE_VALUE_TYPE(UInt64, std::uint64_t, "uint64");
    JBRO_DEFINE_VALUE_TYPE(Float,  float,         "float");
    JBRO_DEFINE_SCALAR_TYPE(std::int8_t,   "int8");
    JBRO_DEFINE_SCALAR_TYPE(std::int16_t,  "int16");
    JBRO_DEFINE_SCALAR_TYPE(std::uint8_t,  "uint8");
    JBRO_DEFINE_SCALAR_TYPE(std::uint16_t, "uint16");
    JBRO_DEFINE_SCALAR_TYPE(double,        "double");
}
