#pragma once

#include <JBro/Reflection/ScalarCodec.h>

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
    // 새 타입을 지원하는 비용은 특수화 하나다. `Vec2` 처럼 구조를 가진 타입은 자기 모듈에서
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

    JBRO_DEFINE_SCALAR_TYPE(bool,          "bool");
    JBRO_DEFINE_SCALAR_TYPE(std::int8_t,   "int8");
    JBRO_DEFINE_SCALAR_TYPE(std::int16_t,  "int16");
    JBRO_DEFINE_SCALAR_TYPE(std::int32_t,  "int32");
    JBRO_DEFINE_SCALAR_TYPE(std::int64_t,  "int64");
    JBRO_DEFINE_SCALAR_TYPE(std::uint8_t,  "uint8");
    JBRO_DEFINE_SCALAR_TYPE(std::uint16_t, "uint16");
    JBRO_DEFINE_SCALAR_TYPE(std::uint32_t, "uint32");
    JBRO_DEFINE_SCALAR_TYPE(std::uint64_t, "uint64");
    JBRO_DEFINE_SCALAR_TYPE(float,         "float");
    JBRO_DEFINE_SCALAR_TYPE(double,        "double");
}
