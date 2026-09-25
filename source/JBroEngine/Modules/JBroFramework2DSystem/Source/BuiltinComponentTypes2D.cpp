#include <JBro/Framework2DSystem/BuiltinComponentTypes2D.h>

#include <JBro/AudioTypes/Component/AudioSource.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Framework2D/Component/AudioListener2D.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>

namespace JBro::Component
{
    bool RegisterBuiltinComponentTypes2D()
    {
        static const bool registered = []
        {
            // && 로 엮지 않는다. 하나가 실패하면 뒤의 것이 아예 등록되지 않고,
            // 그러면 첫 실패 하나가 여러 컴포넌트를 씬에서 통째로 지운다.
            // 갈래와 다중성은 기존 엔진의 등록표를 그대로 옮긴 값이다(D-180).
            // 콜라이더와 스프라이트는 한 오브젝트에 여럿 붙지만, 위치·카메라·강체는
            // 하나뿐이다 - 둘씩 있으면 어느 쪽이 쓰이는지 사용자가 알 수 없다.
            bool all = true;
            all = RegisterComponentType<Transform2D>(
                      ComponentCategory::Transform, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<Camera2D>(
                      ComponentCategory::Rendering, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<SpriteRenderer2D>(ComponentCategory::Rendering) && all;
            // 기존 엔진 표와 같다: Rendering, 여럿 붙는다(D-200).
            all = RegisterComponentType<Text2D>(ComponentCategory::Rendering) && all;
            all = RegisterComponentType<Rigidbody2D>(
                      ComponentCategory::Physics, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<Collider2D>(ComponentCategory::Physics) && all;
            // 소스는 한 오브젝트에 여럿 붙는다(발소리와 숨소리). 리스너는 하나다(D-197).
            all = RegisterComponentType<AudioSource>(ComponentCategory::Audio) && all;
            all = RegisterComponentType<AudioListener2D>(
                      ComponentCategory::Audio, ComponentMultiplicity::Single) && all;
            return all;
        }();
        return registered;
    }
}
