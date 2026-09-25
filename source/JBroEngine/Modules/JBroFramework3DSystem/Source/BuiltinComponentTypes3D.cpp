#include <JBro/Framework3DSystem/BuiltinComponentTypes3D.h>

#include <JBro/AudioTypes/Component/AudioSource.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Framework3D/Component/AudioListener3D.h>
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
            // 2D 쪽과 같은 갈래·다중성을 쓴다(D-180).
            bool all = true;
            all = RegisterComponentType<Transform3D>(
                      ComponentCategory::Transform, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<Camera3D>(
                      ComponentCategory::Rendering, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<MeshRenderer3D>(ComponentCategory::Rendering) && all;
            all = RegisterComponentType<Rigidbody3D>(
                      ComponentCategory::Physics, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<Collider3D>(ComponentCategory::Physics) && all;
            all = RegisterComponentType<AudioSource>(ComponentCategory::Audio) && all;
            all = RegisterComponentType<AudioListener3D>(
                      ComponentCategory::Audio, ComponentMultiplicity::Single) && all;
            return all;
        }();
        return registered;
    }
}
