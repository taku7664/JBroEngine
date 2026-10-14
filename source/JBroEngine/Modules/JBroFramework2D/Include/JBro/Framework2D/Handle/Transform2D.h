#pragma once

#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Angle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class Transform2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Transform2D` 다(D-302). 데이터는 엔진의 `Component::Transform2D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 위치·회전·크기와 시스템이 채우는 월드 캐시다. **월드 캐시는 읽기만 한다** - 저작 값에서 다시 계산되는 것이라 쓰는 멤버가 없다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class Transform2D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<Transform2D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::Transform2D";
        }

        Vector2 GetPosition() const;
        void SetPosition(Vector2 value);

        // 회전은 도(°)다. 담는 것은 라디안이고 바꾸는 것은 컴포넌트가 한다(D-248).
        Degree GetRotation() const;
        void SetRotation(Degree value);
        Vector2 GetScale() const;
        void SetScale(Vector2 value);

        // 화면 레이어의 루트가 붙는 화면의 점이다(0..1, D-237). 월드 레이어와 자식에는 뜻이 없다.
        Vector2 GetAnchor() const;
        void SetAnchor(Vector2 value);

        // 아래는 `Transform2DSystem` 이 채우는 월드 캐시다. `GetWorldValid` 가 거짓인 동안의 값은 읽지 않는다.
        Matrix3x2 GetWorld() const;
        Vector2 GetWorldPosition() const;

        // 월드 회전도 도로 준다(캐시는 라디안이다).
        Degree GetWorldRotation() const;
        Vector2 GetWorldScale() const;
        Bool GetWorldValid() const;

    private:
        Component::Transform2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Transform2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Transform2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Transform2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Transform2D, target)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Transform2D);
