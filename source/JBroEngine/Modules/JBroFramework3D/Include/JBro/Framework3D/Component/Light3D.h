#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Light3D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Light3D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Field.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Float.h>

#include <cstdint>

namespace JBro::Component
{
    // 3D 라이트의 종류다(D-366). `Directional` 은 오브젝트의 앞(-Z)으로 비추고, `Point` 는 오브젝트 자리에서 사방으로 `range` 까지 비추며, `Ambient` 는
    // 어디든 같은 밝기를 더한다.
    enum class Light3DType : std::uint8_t
    {
        Directional,
        Point,
        Ambient,
    };

}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::Light3DType, "JBro.Component.Light3DType",
        { Component::Light3DType::Directional, "Directional" },
        { Component::Light3DType::Point,       "Point" },
        { Component::Light3DType::Ambient,     "Ambient" });
}

namespace JBro::Component
{
    // **3D 라이트**(D-366). 켜진 라이트가 하나라도 있는 뷰는 메시를 이 라이트들로 비춘다. 하나도 없으면 렌더러의 고정 방향광이다. 그림자는 아직 없다.
    class Light3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Light3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Light3D)

        JBRO_FIELD(Light3DType, type) = Light3DType::Directional;
        JBRO_FIELD(Color, color) { 1.0f, 1.0f, 1.0f, 1.0f };
        JBRO_FIELD(Float, intensity, Clamp(0.0f, 100.0f)) = 1.0f;
        // `Point` 의 빛이 0 이 되는 거리다(월드 단위).
        JBRO_FIELD(Float, range, Clamp(0.0f, 100000.0f)) = 10.0f;
    };
}
