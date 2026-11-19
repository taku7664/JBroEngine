#include <JBro/Framework2D/BuiltinComponentProperties2D.h>

#include <JBro/AudioTypes/BuiltinAudioComponents.h>
#include <JBro/Framework2D/Component/AudioListener2D.h>
#include <JBro/Framework2D/Component/InputField2D.h>
#include <JBro/Framework2D/Component/Layout2D.h>
#include <JBro/Framework2D/Component/Light2D.h>
#include <JBro/Framework2D/Component/Mask2D.h>
#include <JBro/Framework2D/Component/ScrollView2D.h>
#include <JBro/Framework2D/Component/Slider2D.h>
#include <JBro/Framework2D/Component/Toggle2D.h>
#include <JBro/Framework2D/Component/ToggleGroup2D.h>
#include <JBro/Framework2D/Component/Dropdown2D.h>
#include <JBro/Framework2D/Component/ContentSizeFitter2D.h>
#include <JBro/Framework2D/Component/Tooltip2D.h>
#include <JBro/Framework2D/Component/Modal2D.h>
#include <JBro/Framework2D/Component/TabGroup2D.h>
#include <JBro/Framework2D/Component/Draggable2D.h>
#include <JBro/Framework2D/Component/DropTarget2D.h>
#include <JBro/Framework2D/Component/Tween2D.h>
#include <JBro/Framework2D/Component/ListView2D.h>
#include <JBro/Framework2D/Component/SafeArea2D.h>
#include <JBro/Framework2D/Component/CanvasGroup2D.h>
#include <JBro/Framework2D/Component/AspectRatioFitter2D.h>
#include <JBro/Framework2D/Component/VirtualKeyboard2D.h>
#include <JBro/Framework2D/Component/Navigation2D.h>
#include <JBro/Framework2D/Component/ShadowCaster2D.h>

#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/PostProcessVolume2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Types/Bool.h>

namespace JBro::Component
{
    Bool RegisterBuiltinComponentProperties2D()
    {
        // 한 번만 실제로 등록한다. 두 번째부터는 보관함이 중복을 거절하므로
        // 그것을 실패로 되돌려 주면 부르는 쪽이 순서를 신경 써야 한다.
        static const Bool registered = []
        {
            // && 로 엮지 않는다. 하나가 실패하면 뒤의 것이 아예 등록되지 않고,
            // 그러면 첫 실패 하나가 인스펙터에서 여러 컴포넌트를 통째로 지운다.
            Bool all = true;
            all = RegisterBuiltinProperties<Transform2D>()      && all;
            all = RegisterBuiltinProperties<Camera2D>()         && all;
            all = RegisterBuiltinProperties<PostProcessVolume2D>() && all;
            all = RegisterBuiltinProperties<SpriteRenderer2D>() && all;
            all = RegisterBuiltinProperties<Light2D>() && all;
            all = RegisterBuiltinProperties<Slider2D>() && all;
            all = RegisterBuiltinProperties<Toggle2D>() && all;
            all = RegisterBuiltinProperties<ToggleGroup2D>() && all;
            all = RegisterBuiltinProperties<Dropdown2D>() && all;
            all = RegisterBuiltinProperties<ContentSizeFitter2D>() && all;
            all = RegisterBuiltinProperties<Tooltip2D>() && all;
            all = RegisterBuiltinProperties<Modal2D>() && all;
            all = RegisterBuiltinProperties<TabGroup2D>() && all;
            all = RegisterBuiltinProperties<Draggable2D>() && all;
            all = RegisterBuiltinProperties<DropTarget2D>() && all;
            all = RegisterBuiltinProperties<Tween2D>() && all;
            all = RegisterBuiltinProperties<ListView2D>() && all;
            all = RegisterBuiltinProperties<SafeArea2D>() && all;
            all = RegisterBuiltinProperties<CanvasGroup2D>() && all;
            all = RegisterBuiltinProperties<AspectRatioFitter2D>() && all;
            all = RegisterBuiltinProperties<VirtualKeyboard2D>() && all;
            all = RegisterBuiltinProperties<Navigation2D>() && all;
            all = RegisterBuiltinProperties<Mask2D>() && all;
            all = RegisterBuiltinProperties<ScrollView2D>() && all;
            all = RegisterBuiltinProperties<InputField2D>() && all;
            all = RegisterBuiltinProperties<Layout2D>() && all;
            all = RegisterBuiltinProperties<ShadowCaster2D>() && all;
            all = RegisterBuiltinProperties<Text2D>()           && all;
            all = RegisterBuiltinProperties<Button2D>()         && all;
            all = RegisterBuiltinProperties<Rigidbody2D>()      && all;
            all = RegisterBuiltinProperties<Collider2D>()       && all;
            all = RegisterBuiltinProperties<DistanceJoint2D>()  && all;
            all = RegisterBuiltinProperties<HingeJoint2D>()     && all;
            all = RegisterBuiltinProperties<AudioListener2D>()  && all;
            // 소스는 차원과 무관한 모듈의 것이다(D-197). 두 프레임워크가 함께 부르고 한 번만 등록된다.
            all = RegisterBuiltinAudioComponentProperties()    && all;
            return all;
        }();
        return registered;
    }
}
