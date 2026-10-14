#pragma once

#include <JBro/Framework2D/ComponentTypes2D.h>
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
    class Camera2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Camera2D` 다(D-302). 데이터는 엔진의 `Component::Camera2D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 2D 카메라다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class Camera2D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<Camera2D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::Camera2D";
        }

        Component::CameraProjection2D GetProjection() const;
        void SetProjection(Component::CameraProjection2D value);

        // `Orthographic` 의 화면 세로 절반이 담는 월드 길이다.
        Float GetOrthographicSize() const;
        void SetOrthographicSize(Float value);

        // `PixelPerfect` 에서 원본 몇 픽셀이 1 유닛인가(D-239).
        Float GetPixelsPerUnit() const;
        void SetPixelsPerUnit(Float value);
        Float GetNearPlane() const;
        void SetNearPlane(Float value);
        Float GetFarPlane() const;
        void SetFarPlane(Float value);
        Color GetClearColor() const;
        void SetClearColor(Color value);
        Bool GetPrimary() const;
        void SetPrimary(Bool value);

    private:
        Component::Camera2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Camera2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Camera2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Camera2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Camera2D, target)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Camera2D);
