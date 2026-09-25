#include <JBro/Framework2D/BuiltinComponentProperties2D.h>

#include <JBro/AudioTypes/BuiltinAudioComponents.h>
#include <JBro/Framework2D/Component/AudioListener2D.h>

#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Reflection/PropertyRegistry.h>

namespace JBro::Component
{
    bool RegisterBuiltinComponentProperties2D()
    {
        // 한 번만 실제로 등록한다. 두 번째부터는 보관함이 중복을 거절하므로
        // 그것을 실패로 되돌려 주면 부르는 쪽이 순서를 신경 써야 한다.
        static const bool registered = []
        {
            // && 로 엮지 않는다. 하나가 실패하면 뒤의 것이 아예 등록되지 않고,
            // 그러면 첫 실패 하나가 인스펙터에서 여러 컴포넌트를 통째로 지운다.
            bool all = true;
            all = RegisterBuiltinProperties<Transform2D>()      && all;
            all = RegisterBuiltinProperties<Camera2D>()         && all;
            all = RegisterBuiltinProperties<SpriteRenderer2D>() && all;
            all = RegisterBuiltinProperties<Text2D>()           && all;
            all = RegisterBuiltinProperties<Rigidbody2D>()      && all;
            all = RegisterBuiltinProperties<Collider2D>()       && all;
            all = RegisterBuiltinProperties<AudioListener2D>()  && all;
            // 소스는 차원과 무관한 모듈의 것이다(D-197). 두 프레임워크가 함께 부르고 한 번만 등록된다.
            all = RegisterBuiltinAudioComponentProperties()    && all;
            return all;
        }();
        return registered;
    }
}
