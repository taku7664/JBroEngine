#include <JBro/Framework3DSystem/BuiltinComponentTypes3D.h>

#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Physics3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>

namespace JBro::Component
{
    bool RegisterBuiltinComponentTypes3D()
    {
        static const bool registered = []
        {
            // && 로 엮지 않는다. 하나가 실패하면 뒤의 것이 아예 등록되지 않고,
            // 그러면 첫 실패 하나가 여러 컴포넌트를 씬에서 통째로 지운다.
            bool all = true;
            all = RegisterComponentType<Transform3D>()    && all;
            all = RegisterComponentType<Camera3D>()       && all;
            all = RegisterComponentType<MeshRenderer3D>() && all;
            all = RegisterComponentType<Rigidbody3D>()    && all;
            all = RegisterComponentType<Collider3D>()     && all;
            return all;
        }();
        return registered;
    }
}
