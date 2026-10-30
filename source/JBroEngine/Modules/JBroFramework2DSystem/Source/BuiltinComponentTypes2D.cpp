#include <JBro/Framework2DSystem/BuiltinComponentTypes2D.h>

#include <JBro/AudioTypes/Component/AudioSource.h>
#include <JBro/Canvas/ComponentRegistry.h>
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
#include <JBro/Framework2D/Component/ShadowCaster2D.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/PostProcessVolume2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Types/Bool.h>

namespace JBro::Component
{
    Bool RegisterBuiltinComponentTypes2D()
    {
        static const Bool registered = []
        {
            // && 로 엮지 않는다. 하나가 실패하면 뒤의 것이 아예 등록되지 않고,
            // 그러면 첫 실패 하나가 여러 컴포넌트를 씬에서 통째로 지운다.
            // 갈래와 다중성은 기존 엔진의 등록표를 그대로 옮긴 값이다(D-180).
            // 콜라이더와 스프라이트는 한 오브젝트에 여럿 붙지만, 위치·카메라·강체는
            // 하나뿐이다 - 둘씩 있으면 어느 쪽이 쓰이는지 사용자가 알 수 없다.
            Bool all = true;
            all = RegisterComponentType<Transform2D>(
                      ComponentCategory::Transform, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<Camera2D>(
                      ComponentCategory::Rendering, ComponentMultiplicity::Single) && all;
            // 볼륨은 오브젝트에 하나다 - 모양이 오브젝트의 트랜스폼에 놓이므로 둘이면 같은 자리의 두 프로필이다(D-346).
            all = RegisterComponentType<PostProcessVolume2D>(
                      ComponentCategory::Rendering, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<SpriteRenderer2D>(ComponentCategory::Rendering) && all;
            // 기존 엔진 표와 같다: Rendering, 여럿 붙는다(D-200).
            all = RegisterComponentType<Text2D>(ComponentCategory::Rendering) && all;
            all = RegisterComponentType<Light2D>(ComponentCategory::Rendering) && all;
            all = RegisterComponentType<ShadowCaster2D>(ComponentCategory::Rendering) && all;
            all = RegisterComponentType<Rigidbody2D>(
                      ComponentCategory::Physics, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<Collider2D>(ComponentCategory::Physics) && all;
            // 조인트는 한 오브젝트에 여럿 붙는다(사슬의 양쪽 고리, D-233).
            all = RegisterComponentType<DistanceJoint2D>(ComponentCategory::Physics) && all;
            all = RegisterComponentType<HingeJoint2D>(ComponentCategory::Physics) && all;
            // 소스는 한 오브젝트에 여럿 붙는다(발소리와 숨소리). 리스너는 하나다(D-197).
            all = RegisterComponentType<AudioSource>(ComponentCategory::Audio) && all;
            all = RegisterComponentType<AudioListener2D>(
                      ComponentCategory::Audio, ComponentMultiplicity::Single) && all;
            // 누름 사각형은 하나다 - 둘이면 어느 것이 훅을 부르는지 사용자가 알 수 없다(D-237).
            all = RegisterComponentType<Button2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            // 토글·슬라이더도 누름 사각형 하나다(D-351).
            all = RegisterComponentType<Toggle2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<Slider2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            // 마스크와 스크롤 뷰도 오브젝트에 하나다 - 사각형이 하나다.
            all = RegisterComponentType<Mask2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<ScrollView2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<InputField2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<Layout2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            // 토글 그룹은 오브젝트에 하나(어느 그룹인지 하나여야 한다), 드롭다운은 누름 사각형 하나다(D-355).
            all = RegisterComponentType<ToggleGroup2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<Dropdown2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<ContentSizeFitter2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            return all;
        }();
        return registered;
    }
}
