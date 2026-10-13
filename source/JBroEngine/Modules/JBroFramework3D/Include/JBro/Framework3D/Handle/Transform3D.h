#pragma once

#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math3D.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class Transform3D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Transform3D` 다(D-302). 데이터는 엔진의 `Component::Transform3D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 위치·회전·크기와 시스템이 채우는 월드 캐시다. **월드 캐시는 읽기만 한다.**
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class Transform3D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<Transform3D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::Transform3D";
        }

        Vector3 GetPosition() const;
        void SetPosition(Vector3 value);
        Quaternion GetRotation() const;
        void SetRotation(Quaternion value);
        Vector3 GetScale() const;
        void SetScale(Vector3 value);

        // 아래는 `Transform3DSystem` 이 채우는 월드 캐시다. `GetWorldValid` 가 거짓인 동안의 값은 읽지 않는다.
        Vector3 GetWorldPosition() const;
        Quaternion GetWorldRotation() const;
        Vector3 GetWorldScale() const;
        Bool GetWorldValid() const;

    private:
        Component::Transform3D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Transform3D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Transform3D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Transform3D>, "a component handle must stay trivially copyable");
}
