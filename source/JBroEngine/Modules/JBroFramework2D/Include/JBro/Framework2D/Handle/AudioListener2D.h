#pragma once

#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class AudioListener2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `AudioListener2D` 다(D-302). 데이터는 엔진의 `Component::AudioListener2D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 소리를 듣는 자리다(D-197).
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class AudioListener2D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<AudioListener2D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::AudioListener2D";
        }

        Float GetPanDistance() const;
        void SetPanDistance(Float value);
        Float GetVolume() const;
        void SetVolume(Float value);

    private:
        Component::AudioListener2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(AudioListener2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<AudioListener2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<AudioListener2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(AudioListener2D, target)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::AudioListener2D);
