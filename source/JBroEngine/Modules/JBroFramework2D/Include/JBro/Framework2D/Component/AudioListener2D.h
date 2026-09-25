#pragma once

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Runtime/Component.h>

namespace JBro::Component
{
    // 소리를 듣는 자리다(D-197). 붙인 오브젝트의 `Transform2D` 월드 위치에서 듣는다. **캔버스에 하나를 쓴다** - 여럿이면
    // 첫 활성 리스너를 쓰고 경고를 한 번 남긴다. 하나도 없으면 게임을 그리는 카메라(`Camera2D`)의 자리에서 듣는다.
    //
    // 2D 에는 앞뒤가 없어 방향은 늘 같다(화면을 보는 쪽). 공간화한 소스(`AudioSource::spatial`)만 영향을 받는다.
    class AudioListener2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::AudioListener2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(AudioListener2D)

        // 좌우로 이만큼 떨어진 소리가 한쪽으로 크게(약 45°) 기운다(월드 단위). 작을수록 가까운 소리도 한쪽 귀로 쏠린다.
        // 화면 폭의 절반쯤이 자연스럽다.
        JBRO_FIELD(float, panDistance, Range(0.1f, 1000.0f)) = 5.0f;
    };
}
