#pragma once

#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>

#include <type_traits>

namespace JBro::Component
{
    class BoneAttachment3D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `BoneAttachment3D` 다(D-302, D-460). 데이터는 엔진의 `Component::BoneAttachment3D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 소켓의 월드 자리를 읽는 길은 `GetFramework3DServices().Animation3D` 다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않는다.
    class BoneAttachment3D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::BoneAttachment3D";
        }

        // 붙을 소켓(또는 뼈)의 이름을 바꾼다. 다음 프레임의 붙이기 시스템이 새 이름을 한 번 푼다. null 은 빈 이름이다(붙지 않는다).
        void SetSocket(const char* name);

    private:
        Component::BoneAttachment3D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(BoneAttachment3D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<BoneAttachment3D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<BoneAttachment3D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(BoneAttachment3D, sword)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::BoneAttachment3D);
