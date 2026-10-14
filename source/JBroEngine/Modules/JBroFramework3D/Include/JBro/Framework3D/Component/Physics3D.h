#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Rigidbody3D`·`Collider3D`)로만 만진다(D-302).
// 정규화한 이름으로 써도 스크립트 대상 빌드에서는 여기서 컴파일이 실패한다.
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches the 3D physics components through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Reflection/Math3DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Float.h>

namespace JBro::Component
{
    class Rigidbody3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Rigidbody3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Rigidbody3D)

        // 시뮬레이션이 매 프레임 다시 쓴다. 저장하면 씬을 열 때마다
        // 물체가 저장된 순간의 속도로 튀어 나간다(2D 쪽과 같은 이유).
        JBRO_FIELD(JBro::Vector3, velocity, NoSerialize());
        JBRO_FIELD(Float,      mass, Range(0.0f, 1000.0f)) = 1.0f;
    };

    class Collider3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Collider3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Collider3D)

        // 골격이다(D-53). 2D 쪽에 있는 shape·offset·isTrigger 가 아직 없다.
        JBRO_FIELD(JBro::Vector3, size) { 1.0f, 1.0f, 1.0f };
    };
}
