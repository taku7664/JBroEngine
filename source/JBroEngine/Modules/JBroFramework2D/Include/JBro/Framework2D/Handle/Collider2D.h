#pragma once

#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class Collider2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Collider2D` 다(D-302). 데이터는 엔진의 `Component::Collider2D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 2D 콜라이더다. 레이어와 마스크는 비트 묶음이다 - 비트 i 의 이름은 프로젝트 설정의 물리 레이어 i 번째다(D-233).
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class Collider2D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<Collider2D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::Collider2D";
        }

        Component::ColliderShape2D GetShape() const;
        void SetShape(Component::ColliderShape2D value);
        Vector2 GetOffset() const;
        void SetOffset(Vector2 value);
        Vector2 GetSize() const;
        void SetSize(Vector2 value);
        Float GetRadius() const;
        void SetRadius(Float value);
        Bool GetIsTrigger() const;
        void SetIsTrigger(Bool value);

        // `Polygon`·`Chain` 의 포인트다(오브젝트 로컬). `points` 를 덮어쓴다 - 무효면 비운다.
        void GetPoints(Array<Vector2>& points) const;
        void SetPoints(const Array<Vector2>& points);
        Bool GetLoop() const;
        void SetLoop(Bool value);
        Bool GetOneWay() const;
        void SetOneWay(Bool value);
        Float GetFriction() const;
        void SetFriction(Float value);
        Float GetRestitution() const;
        void SetRestitution(Float value);
        PhysicsLayerMask GetLayer() const;
        void SetLayer(PhysicsLayerMask value);
        PhysicsLayerMask GetMask() const;
        void SetMask(PhysicsLayerMask value);

    private:
        Component::Collider2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Collider2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Collider2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Collider2D>, "a component handle must stay trivially copyable");
}
