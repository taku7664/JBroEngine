#include <JBro/Framework2DSystem/BuiltinComponentTypes2D.h>

#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>

namespace JBro::Component
{
    bool RegisterBuiltinComponentTypes2D()
    {
        static const bool registered = []
        {
            // && 로 엮지 않는다. 하나가 실패하면 뒤의 것이 아예 등록되지 않고,
            // 그러면 첫 실패 하나가 여러 컴포넌트를 씬에서 통째로 지운다.
            bool all = true;
            all = RegisterComponentType<Transform2D>()      && all;
            all = RegisterComponentType<Camera2D>()         && all;
            all = RegisterComponentType<SpriteRenderer2D>() && all;
            all = RegisterComponentType<Rigidbody2D>()      && all;
            all = RegisterComponentType<Collider2D>()       && all;
            return all;
        }();
        return registered;
    }
}
