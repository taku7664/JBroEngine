#include <JBro/ScreenUi/ScreenUiComponentTypes.h>

#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Framework2D/Component/Toggle2D.h>
#include <JBro/Framework2D/Component/Slider2D.h>
#include <JBro/Framework2D/Component/Mask2D.h>
#include <JBro/Framework2D/Component/ScrollView2D.h>
#include <JBro/Framework2D/Component/InputField2D.h>
#include <JBro/Framework2D/Component/Layout2D.h>
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

namespace JBro::Component
{
    Bool RegisterScreenUiComponentTypes()
    {
        static const Bool registered = []
        {
            // && 로 엮지 않는다. 하나가 실패하면 뒤의 것이 아예 등록되지 않는다(`RegisterBuiltinComponentTypes2D` 와 같은 까닭).
            Bool all = true;
            all = RegisterComponentType<Transform2D>(
                      ComponentCategory::Transform, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<SpriteRenderer2D>(ComponentCategory::Rendering) && all;
            // 기존 엔진 표와 같다: Rendering, 여럿 붙는다(D-200).
            all = RegisterComponentType<Text2D>(ComponentCategory::Rendering) && all;
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
                      ComponentCategory::UiLayout, ComponentMultiplicity::Single) && all;
            // 토글 그룹은 오브젝트에 하나(어느 그룹인지 하나여야 한다), 드롭다운은 누름 사각형 하나다(D-355).
            all = RegisterComponentType<ToggleGroup2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<Dropdown2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<ContentSizeFitter2D>(
                      ComponentCategory::UiLayout, ComponentMultiplicity::Single) && all;
            // B 2 순위(D-357): 툴팁·모달·탭·끌기·놓을 자리. 사각형이 있는 것은 하나, 나머지도 오브젝트에 하나다.
            all = RegisterComponentType<Tooltip2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<Modal2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<TabGroup2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<Draggable2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<DropTarget2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            // 트윈은 한 오브젝트에 여럿이다(자리와 알파를 같이). 목록은 스크롤 뷰 하나에 하나다.
            all = RegisterComponentType<Tween2D>(ComponentCategory::UI) && all;
            all = RegisterComponentType<ListView2D>(
                      ComponentCategory::UiLayout, ComponentMultiplicity::Single) && all;
            // B 3 순위(D-358): 안전 영역·그룹·가로세로비·가상 키보드·탐색 연결. 모두 오브젝트에 하나다.
            all = RegisterComponentType<SafeArea2D>(
                      ComponentCategory::UiLayout, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<CanvasGroup2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<AspectRatioFitter2D>(
                      ComponentCategory::UiLayout, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<VirtualKeyboard2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            all = RegisterComponentType<Navigation2D>(
                      ComponentCategory::UI, ComponentMultiplicity::Single) && all;
            return all;
        }();
        return registered;
    }

    Bool IsScreenUiComponentType(NameId name)
    {
        static constexpr NameId Names[] =
            {
                MakeNameId(Transform2D::StaticTypeName()),
                MakeNameId(SpriteRenderer2D::StaticTypeName()),
                MakeNameId(Text2D::StaticTypeName()),
                MakeNameId(Button2D::StaticTypeName()),
                MakeNameId(Toggle2D::StaticTypeName()),
                MakeNameId(Slider2D::StaticTypeName()),
                MakeNameId(Mask2D::StaticTypeName()),
                MakeNameId(ScrollView2D::StaticTypeName()),
                MakeNameId(InputField2D::StaticTypeName()),
                MakeNameId(Layout2D::StaticTypeName()),
                MakeNameId(ToggleGroup2D::StaticTypeName()),
                MakeNameId(Dropdown2D::StaticTypeName()),
                MakeNameId(ContentSizeFitter2D::StaticTypeName()),
                MakeNameId(Tooltip2D::StaticTypeName()),
                MakeNameId(Modal2D::StaticTypeName()),
                MakeNameId(TabGroup2D::StaticTypeName()),
                MakeNameId(Draggable2D::StaticTypeName()),
                MakeNameId(DropTarget2D::StaticTypeName()),
                MakeNameId(Tween2D::StaticTypeName()),
                MakeNameId(ListView2D::StaticTypeName()),
                MakeNameId(SafeArea2D::StaticTypeName()),
                MakeNameId(CanvasGroup2D::StaticTypeName()),
                MakeNameId(AspectRatioFitter2D::StaticTypeName()),
                MakeNameId(VirtualKeyboard2D::StaticTypeName()),
                MakeNameId(Navigation2D::StaticTypeName()),
            };
        for (const NameId candidate : Names)
        {
            if (candidate == name)
            {
                return true;
            }
        }
        return false;
    }
}
