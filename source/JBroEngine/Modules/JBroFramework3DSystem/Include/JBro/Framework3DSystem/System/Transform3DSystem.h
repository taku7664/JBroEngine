#pragma once

#include <JBro/Canvas/GameSystem.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro
{
    namespace Object
    {
        class GameObject;
    }
    namespace Component
    {
        class Transform3D;
    }
}

namespace JBro::System
{
    // 뿌리에서 내려가며 `Transform3D` 의 월드 캐시를 채운다. 2D 와 같은 걸음이다.
    class Transform3DSystem final : public GameSystem
    {
    public:
        Int32 GetExecutionOrder() const override;

        // 이 오브젝트의 월드 캐시를 자식들에게 다시 내린다. 뼈에 붙이기(D-460)가 월드를 바꾼 뒤 부른다.
        static void PropagateChildren(Canvas& canvas, Object::GameObject& object, const Component::Transform3D& transform);

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;
    };
}
