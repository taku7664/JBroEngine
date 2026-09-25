#pragma once

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Runtime/Component.h>

namespace JBro::Component
{
    // 소리를 듣는 자리다(D-197). 붙인 오브젝트의 `Transform3D` 월드 위치와 방향(-Z 가 앞, +Y 가 위)으로 듣는다.
    // **캔버스에 하나를 쓴다** - 여럿이면 첫 활성 리스너를 쓰고 경고를 한 번 남긴다. 하나도 없으면 게임을 그리는
    // 카메라(`Camera3D`)의 자리와 방향으로 듣는다. 공간화한 소스(`AudioSource::spatial`)만 영향을 받는다.
    class AudioListener3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::AudioListener3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(AudioListener3D)
    };
}
