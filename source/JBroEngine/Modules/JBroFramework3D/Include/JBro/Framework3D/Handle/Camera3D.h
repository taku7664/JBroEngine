#pragma once

#include <JBro/Framework3D/ComponentTypes3D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class Camera3D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Camera3D` 다(D-302). 데이터는 엔진의 `Component::Camera3D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 3D 카메라다. 자기 `Transform3D` 의 -Z 를 본다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class Camera3D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<Camera3D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::Camera3D";
        }

        Component::CameraProjection3D GetProjection() const;
        void SetProjection(Component::CameraProjection3D value);

        // 세로 시야각(도). 원근 투영에서만 쓴다.
        Float GetVerticalFieldOfView() const;
        void SetVerticalFieldOfView(Float value);
        Float GetOrthographicSize() const;
        void SetOrthographicSize(Float value);
        Float GetNearPlane() const;
        void SetNearPlane(Float value);
        Float GetFarPlane() const;
        void SetFarPlane(Float value);
        Color GetClearColor() const;
        void SetClearColor(Color value);
        Bool GetPrimary() const;
        void SetPrimary(Bool value);

    private:
        Component::Camera3D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Camera3D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Camera3D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Camera3D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Camera3D, target)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Camera3D);
