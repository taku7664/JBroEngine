#pragma once

#include <JBro/Framework3D/ComponentTypes3D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Angle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math3D.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class CharacterController3D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `CharacterController3D` 다(D-302). 데이터는 엔진의 `Component::CharacterController3D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 캐릭터 컨트롤러다(D-427, 유니티의 CharacterController 와 같은 쓰임). `Move` 로 준 이동을 다음 물리 스텝이 막히면 미끄러지며 따른다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class CharacterController3D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<CharacterController3D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::CharacterController3D";
        }

        // 다음 물리 스텝에 이만큼(월드) 옮긴다. 여러 번 부르면 더한다. 중력은 넣지 않는다 - 떨어지게 하려면 이동에 넣는다.
        void Move(Vector3 motion);
        // 마지막 물리 스텝 뒤 걸을 수 있는 바닥(slopeLimit 이하)에 서 있는가.
        Bool IsGrounded() const;
        // 마지막 물리 스텝에 실제로 간 속도(막혀 덜 갔으면 덜 간 만큼).
        Vector3 GetVelocity() const;

        Vector3 GetCenter() const;
        void SetCenter(Vector3 value);
        Float GetRadius() const;
        void SetRadius(Float value);
        Float GetHeight() const;
        void SetHeight(Float value);
        Degree GetSlopeLimit() const;
        void SetSlopeLimit(Degree value);
        Float GetStepOffset() const;
        void SetStepOffset(Float value);

    private:
        Component::CharacterController3D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(CharacterController3D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<CharacterController3D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<CharacterController3D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(CharacterController3D, target)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::CharacterController3D);
