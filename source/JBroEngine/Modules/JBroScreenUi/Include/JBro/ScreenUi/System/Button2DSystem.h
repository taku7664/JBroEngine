#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Canvas/GameSystem.h>
#include <JBro/Canvas/ScreenSpace.h>
#include <JBro/Framework2D/System/IScreen2DSystem.h>
#include <JBro/InputTypes/InputHandler.h>
#include <JBro/InputTypes/InputState.h>
#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/ScreenUi/UiHost.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace Object
    {
        class GameObject;
    }
    class ITextInputHost;
    class Layer;
}

namespace JBro::Component
{
    class Button2D;
    class Draggable2D;
    class Dropdown2D;
    class InputField2D;
    class ScrollView2D;
    class Slider2D;
    class Toggle2D;
    class ToggleGroup2D;
    class Transform2D;
}

namespace JBro::System
{

    // **UI 위젯과 화면 역투영**(D-237·D-351·D-352, ui-plan §2.5·§6.2). 입력 레이어 `"UI"` 에 선 시스템 핸들러다 - 같은 레이어의 스크립트보다 먼저 받는다.
    //
    // 한 프레임에 포인터(첫 손가락, 없으면 마우스 왼쪽 단추) 아래의 **가장 위 위젯 하나**(버튼·토글·슬라이더·글자 칸·스크롤 뷰)를 고른다. 화면 레이어가
    // 월드 위이고, 같은 공간에서는 레이어 차례, 그다음 같은 오브젝트의 스프라이트 `renderOrder`, 그다음 `drawSequence`(D-296)가 큰 것이 위다. 마스크(`Mask2D`)
    // 밖의 위젯은 없는 것이다. 위젯 위에 있거나 위젯을 쥐고 있으면 그 장치(마우스·터치)를 소비한다 - 아래 레이어의 핸들러와 `InputService` 의 폴링은 빈
    // 포인터를 본다. 입력 체인은 시스템 갱신보다 앞이라 마스크 사각형(`Mask2D.clip*`)은 **지난 프레임의 것**이다 - 마스크가 생기거나 옮겨진 바로 그
    // 프레임은 옛 사각형으로 본다.
    //
    // **포커스**(D-352)는 위젯 하나다. 포인터로 누른 위젯이 포커스를 받고, 게임패드의 십자키·왼쪽 스틱이 그 방향의 가장 가까운 위젯으로 옮기며 `South` 가
    // 누름이다(슬라이더는 좌우가 값이다). 글자 칸에 포커스가 있으면 키보드를 받고 소비한다. 게임패드는 UI 가 쓴 프레임에만 소비한다.
    // 쥔 십자키·스틱과 글자 칸의 Backspace·Delete·←·→ 는 **자동 반복**한다(D-354) - 입력 상태의 눌림 수는 §7.1 대로 OS 반복을 세지 않으므로
    // 이 시스템이 쥔 시간을 들고 `RepeatDelay` 뒤 간격마다 다시 누른 것으로 본다. 시간은 지난 갱신 훅의 델타다(편집 중에는 0 이라 반복하지 않는다).
    // 글자 칸을 누른 자리에 커서가 서고 끌면 선택이며, Shift+화살표가 선택을 늘리고 Ctrl+A/C/X/V 가 호스트의 클립보드로 복사·붙여넣기다(D-355).
    // IME 조합 글자는 입력 상태에서 떠 두었다가 갱신 훅이 보이는 텍스트의 커서 자리에 끼우고, 포커스 칸의 커서 자리를 호스트(`ITextInputHost`)에 알려
    // 조합 창이 따라오게 한다.
    // 토글 그룹(`ToggleGroup2D`)은 조상의 토글 가운데 하나만 켜지게 하고, 드롭다운(`Dropdown2D`)은 누르면 목록을 펴 그 아래의 토글로 고른다(D-355) - 전이는
    // 버튼·토글·슬라이더·글자 칸·드롭다운이 모두 같은 셈(`ApplyTransition`)이다. 포커스가 있는 드롭다운은 Enter·`South` 가 펴고, 펴진 동안 ↑·↓·십자키가 항목을
    // 돋우며(`highlighted`, 호버처럼 보인다) Enter·`South` 가 고른다(D-360).
    // 모달(`Modal2D`)이 켜져 있으면 그 안의 위젯만 후보이고 밖은 없는 것이다(D-357). 툴팁(`Tooltip2D`)은 포인터를 잡지 않고 머문 시간만 센다. 끌기(`Draggable2D`)는
    // 쥔 동안 오브젝트를 포인터만큼 옮기고 놓은 자리(`DropTarget2D`)에 떨어뜨린다 - 훅 `OnDragBegin`·`OnDragEnd`·`OnDrop`.
    //
    // 스크롤 뷰는 뷰포트 안에서 누르고 몇 픽셀 끌면 끌기가 되고, 그때 안의 버튼 누름은 취소된다. 갱신 훅(`OnUpdate`, 트랜스폼 전파보다 먼저)은
    // 값에서 그림을 놓고(슬라이더 손잡이·채우기, 토글 표시, 글자 칸의 글자·커서) 스크롤의 관성을 돌린다.
    //
    // 기존 엔진은 버튼이 장치를 직접 폴링했고, 게임이 `IsPointerOverButton()` 을 손으로 물어야 게임 입력이 막혔다(ui-plan §1.2 U3).
    class Button2DSystem final : public GameSystem, public IInputHandler, public IScreen2DSystem
    {
    public:
        // 끌기로 보는 거리(게임 화면 픽셀). 이보다 덜 움직이고 떼면 누름이다.
        static constexpr Float ScrollDragThreshold = 8.0f;
        // 스틱이 이 값을 넘는 순간이 한 번의 이동이다.
        static constexpr Float StickThreshold = 0.5f;
        // 게임패드로 슬라이더를 한 번 움직이는 양이다.
        static constexpr Float SliderStep = 0.1f;
        // 자동 반복(D-354): 쥔 지 이만큼 지나면 처음 되풀이하고, 그 뒤로는 글자 키는 `KeyRepeatInterval`, 십자키·스틱은 `NavigateRepeatInterval` 마다다.
        static constexpr Float RepeatDelay = 0.4f;
        static constexpr Float KeyRepeatInterval = 0.05f;
        static constexpr Float NavigateRepeatInterval = 0.1f;
        // 더블 클릭(D-361): 같은 글자 칸을 이 시간 안에 이 거리(게임 화면 픽셀) 안에서 다시 누르면 단어 선택이다.
        static constexpr Float DoubleClickTime = 0.3f;
        static constexpr Float DoubleClickDistance = 4.0f;

        Int32 GetExecutionOrder() const override;

        // 입력 체인에 선다. 없으면 위젯은 눌리지 않는다(입력이 없는 호스트).
        void SetInputChain(IUiInputChain* chain);
        // 스크립트 훅을 부르는 자리(D-362). 2D 는 `GameScript2D` 로 부른다. 없으면 훅이 없다.
        void SetHookDispatcher(IUiHookDispatcher* hooks);
        // 월드 레이어의 역투영(D-362). 2D 는 카메라 2D 로 잰다. 없으면 월드 레이어의 위젯은 누를 수 없다(화면 레이어는 그대로다).
        void SetWorldLayerMapper(const IWorldLayerMapper* mapper);
        // 입력 체인의 `"UI"` 레이어에 서는 순서다. 같은 순서면 시스템 핸들러가 스크립트보다 먼저다.
        static constexpr Int32 InputOrder = 0;
        // 글자 칸의 IME 조합 창 자리와 클립보드를 맡는 호스트다(D-355). 없으면 조합 창을 옮기지 않고 복사·붙여넣기가 비어 있다.
        void SetTextInputHost(ITextInputHost* host);
        // 이번 프레임의 화면 기준이다. 프레임워크가 입력 체인보다 먼저 건다.
        void SetScreenSpace(const ScreenSpaceFrame& frame);
        // 뷰포트에 묶인 화면 레이어의 기준이다(D-439). 그 레이어의 버튼은 그 기준으로 누른다 - 뷰포트 밖의 포인터는 그 버튼에 닿지 않는다.
        void SetScreenLayerFrames(ArrayView<const ScreenLayerFrame> frames);

        InputResult OnInput(InputView& input) override;
        void OnUpdate(Canvas& canvas, Float deltaTime) override;

        Bool ScreenToLayer(Vector2 pixel, Handle::GameObject object, Vector2& point) const override;
        Bool LayerToScreen(Vector2 point, Handle::GameObject object, Vector2& pixel) const override;
        Bool IsPointerOverButton() const override;

        // 픽셀을 이 레이어의 좌표로 옮긴다. 월드 레이어는 주 카메라(`Camera2DSystem` 과 같은 고르기)를 쓴다.
        Bool PixelToLayer(const Layer& layer, Float pixelX, Float pixelY, Vector2& point) const;
        Bool LayerToPixel(const Layer& layer, Vector2 point, Float& pixelX, Float& pixelY) const;

    protected:
        void OnInitialize(Canvas& canvas) override;
        void OnShutdown(Canvas& canvas) override;

    private:
        struct Pointer
        {
            Bool  present = false;
            Bool  touch = false;
            Float x = 0.0f;
            Float y = 0.0f;
            Bool  down = false;
            Bool  pressed = false;
            Bool  released = false;
        };

        enum class WidgetKind : std::uint8_t { None, Button, Toggle, Slider, Field, Scroll, Dropdown, Draggable };
        enum class Hook : std::uint8_t { Enter, Exit, Down, Up, Click, ValueChanged, Submit, DragBegin, DragEnd, Drop };

        static Pointer ReadPointer(InputView& input);
        // 포인터를 오브젝트의 로컬 좌표로 옮긴다. 레이어의 역투영과 월드 행렬의 역이다. `layerPoint` 를 주면 레이어 좌표도 준다.
        Bool PointerToLocal(Object::GameObject& owner, const Layer& layer, const Pointer& pointer, Vector2& local, Vector2* layerPoint = nullptr) const;
        // 누름 사각형(`offset` 가운데의 `size`, 크기는 절댓값) 안이고 마스크 안인가. 안이면 로컬 자리도 준다.
        Bool HitRect(Object::GameObject& owner, const Layer& layer, Vector2 size, Vector2 offset, const Pointer& pointer, Vector2& local) const;
        void CallHook(Object::GameObject* object, Hook hook);
        // 값에서 그림을 놓는다(슬라이더 손잡이·채우기, 토글 표시, 글자 칸의 글자와 커서).
        void PlaceWidgets(Canvas& canvas, Float deltaTime);
        // 이 오브젝트나 조상의 스크롤 뷰(가장 가까운 것)다. 없으면 nullptr.
        Object::GameObject* FindScrollAncestor(Object::GameObject* object) const;
        // 이 오브젝트나 조상의 토글 그룹(가장 가까운 것)이다(D-355). 없으면 nullptr.
        Object::GameObject* FindToggleGroup(Object::GameObject* object) const;
        // 포인터·게임패드가 토글을 뒤집는다(D-355). 드롭다운의 항목이면 뒤집지 않고 고르며(값·접기), 그룹의 켜진 토글은 `allowOff` 가 아니면 그대로다.
        // 켜지면 같은 그룹의 나머지를 끈다. 바뀌었으면 참이고 `changed`·`OnValueChanged` 는 여기서 낸다.
        Bool FlipToggle(Object::GameObject& owner, Component::Toggle2D& toggle);
        // 이 토글이 어느 드롭다운의 항목인가(목록 오브젝트의 자손). 찾으면 그 드롭다운의 오브젝트와 항목 번호다.
        Object::GameObject* FindDropdownOfOption(Object::GameObject* option, UInt32& index) const;
        // 열린 드롭다운을 접는다 - `pressed` 가 그 드롭다운이나 그 목록 안이면 둔다. `nullptr` 이면 모두 접는다.
        void CloseDropdownsOutside(Object::GameObject* pressed);
        // 드롭다운의 항목 수(목록 아래의 토글 수)다(D-360).
        Int32 CountOptions(const Component::Dropdown2D& dropdown) const;
        // 드롭다운을 편다 - 돋운 항목은 고른 것, 없으면 첫 항목이다(D-360).
        void OpenDropdown(Component::Dropdown2D& dropdown);
        // 돋운 항목을 `delta` 만큼 옮긴다. 끝에서 멈추고, 없던 돋움은 방향의 첫 끝이다(D-360).
        void MoveHighlight(Component::Dropdown2D& dropdown, Int32 delta);
        // 돋운 항목을 고르고 접는다(값·`changed`·`OnValueChanged`). 돋운 것이 없으면 접기만이다(D-360).
        void PickHighlight(Object::GameObject& owner, Component::Dropdown2D& dropdown);
        // 켜진 모달이 없거나, 켜진 어느 모달의 오브젝트나 그 자손이면 참이다(D-357). 밖의 위젯은 후보가 아니다.
        Bool InsideModal(Object::GameObject* object) const;
        // 끌던 것을 놓는다(D-357): 포인터 아래의 놓을 자리에 떨어뜨리고(`dropped`·`droppedOn`·`OnDrop`), 없고 `snapBack` 이면 제자리로, 그리고 `OnDragEnd`.
        void DropDraggable(Object::GameObject& released, const Pointer& pointer);
        // 이 오브젝트와 자손의 스프라이트·글자 `renderOrder` 에 `delta` 를 더하고 올린 오브젝트를 `m_lifted` 에 적는다(D-360, 끌리는 동안 맨 위에).
        void LiftSubtree(Object::GameObject& root, Int32 delta);
        // `m_lifted` 에 적힌 오브젝트(아직 있는 것)의 `renderOrder` 를 `delta` 만큼 되돌린다(D-361) - 끌리는 동안 붙인 그림은 건드리지 않는다.
        void LowerLifted(Int32 delta);
        // 이 오브젝트나 조상의 가상 키보드(가장 가까운 것)다(D-358). 없으면 nullptr.
        Object::GameObject* FindKeyboardAncestor(Object::GameObject* object) const;
        // 가상 키보드의 버튼이 눌렸다(D-358): 대상 칸(없으면 포커스 칸)에 글자·지우기·제출을 넣는다. 게임패드로 눌렀으면(D-360) 포커스가 키에 있으니 마지막 포커스 칸까지 본다.
        // `key` 가 nullptr 이면 지우기다(`East`).
        void PressVirtualKey(Object::GameObject& keyboardOwner, Object::GameObject* key, Bool fromGamepad);
        // 글자 칸에 글자를 끼우거나(`utf8`, 선택을 대신한다) 앞 글자를 지운다(`backspace`). 바뀌면 `changed`·`OnValueChanged` 다.
        void EditField(Object::GameObject& owner, Component::InputField2D& field, const char* utf8, std::size_t size, Bool backspace);
        // 이 오브젝트에 켜진 위젯이 있으면 그 종류다(버튼·토글·슬라이더·글자 칸·드롭다운 차례). 없으면 `None`.
        WidgetKind WidgetKindOf(Object::GameObject* object) const;
        // 이 슬라이더가 어느 스크롤 뷰의 막대면 참이고, 한 페이지(뷰포트 하나)가 값으로 얼마인지와 손잡이 반 길이(트랙 비율)를 준다(D-360).
        Bool ScrollbarPage(Object::GameObject& barOwner, Float& step, Float& knobHalf) const;
        // 막대를 `forward` 쪽으로 한 페이지 옮긴다. 손잡이가 포인터에 닿았거나 포인터가 반대쪽이면(지나쳤거나 포인터가 옮겨 갔다) 아무것도 하지 않는다(D-360).
        void PageScrollbar(Object::GameObject& barOwner, Component::Slider2D& slider, Float pointerValue, Bool forward);
        // 소리 한 번(D-358). 핸들이 비면 없다. 오디오가 없는 호스트에서는 아무 일도 없다.
        static void PlayUiSound(AssetHandle sound);
        // 스크롤 뷰의 내용을 옮기고 가둔다. 가둬진 축은 `clampedX`·`clampedY` 가 참이다.
        void ScrollBy(Object::GameObject& owner, Component::ScrollView2D& scroll, Vector2 delta, Bool& clampedX, Bool& clampedY);
        // 포커스를 옮긴다. `nullptr` 은 비우기다. `visible` 은 게임패드·키보드로 옮긴 포커스다 - 그때만 버튼이 호버처럼 보인다(포인터로 누른 것은 포인터가 떠나면 보통이다).
        void SetFocus(Object::GameObject* object, WidgetKind kind, Bool visible = false);
        // 포커스를 그 방향(화면 픽셀, y 아래)의 가장 가까운 위젯으로 옮긴다. 없던 포커스는 왼쪽 위의 위젯이다.
        void NavigateFocus(Int32 directionX, Int32 directionY);
        // 글자 칸에 이번 프레임의 키보드를 넣는다.
        void TypeInto(Object::GameObject& owner, Component::InputField2D& field, InputView& input);
        // 포인터가 가리키는 글자에 커서를 놓는다(D-355). `extend` 면 누른 자리(`m_fieldPressCaret`)부터 선택이다. 레이아웃이 없으면 글자 끝이다.
        void PlaceCaretAt(Component::InputField2D& field, const Pointer& pointer, Bool extend);
        // 커서 자리의 단어(공백이 아닌 글자의 이어짐)를 고른다(D-361, 더블 클릭). 커서가 공백에 있으면 아무것도 고르지 않는다.
        void SelectWordAtCaret(Component::InputField2D& field);
        // 글자를 고치기 전의 글자·커서를 떠 둔다(D-361, Ctrl+Z 한 단계).
        void KeepUndo(Component::InputField2D& field, const char* utf8, std::size_t size, UInt32 caret);
        // 쥔 키·버튼의 자동 반복이다. 이번 프레임에 눌렀으면 참이고 시간을 처음부터 세며, 쥐고 있으면 `RepeatDelay` 뒤 `interval` 마다 참이다.
        Bool Repeats(Bool down, Bool pressed, Float& held, Float interval);
        // 포커스 위젯의 자리(화면 픽셀)다. 없으면 거짓이다.
        Bool WidgetPixel(Object::GameObject& owner, Float& pixelX, Float& pixelY) const;

        Canvas*          m_canvas = nullptr;
        IUiInputChain*   m_inputChain = nullptr;
        IUiHookDispatcher* m_hooks = nullptr;
        const IWorldLayerMapper* m_world = nullptr;
        ScreenSpaceFrame m_screen;
        Array<ScreenLayerFrame> m_layerFrames;
        // 오브젝트 번호로 기억한다 - 지난 프레임의 포인터가 가리키던 오브젝트가 지워졌을 수 있다. 종류는 같은 오브젝트의 어느 위젯인가다.
        InstanceId       m_hovered = InvalidInstanceId;
        WidgetKind       m_hoveredKind = WidgetKind::None;
        InstanceId       m_pressed = InvalidInstanceId;
        WidgetKind       m_pressedKind = WidgetKind::None;
        Bool             m_pointerOver = false;
        // 포커스 위젯과 게임패드의 누름·스틱 상태.
        InstanceId       m_focused = InvalidInstanceId;
        WidgetKind       m_focusedKind = WidgetKind::None;
        // 마지막으로 포커스였던 글자 칸이다(D-360) - 게임패드로 가상 키보드를 누를 때의 대상.
        InstanceId       m_lastField = InvalidInstanceId;
        Bool             m_padPressed = false;
        Bool             m_focusVisible = false;
        Float            m_stickX = 0.0f;
        Float            m_stickY = 0.0f;
        // 자동 반복의 쥔 시간(D-354). 키는 Backspace·Delete·←·→ 차례, 탐색은 쥔 방향과 시간이다. 델타는 지난 갱신 훅의 것이다.
        Float            m_keyHeld[4] = {};
        // 드롭다운의 ↑·↓ 쥔 시간(D-360).
        Float            m_dropdownHeld[2] = {};
        Float            m_navigateHeld = 0.0f;
        Int32            m_navigateHeldX = 0;
        Int32            m_navigateHeldY = 0;
        Float            m_frameDelta = 0.0f;
        // 이번 프레임에 켜진 모달이 있는가, 그 가운데 게임까지 막는 것이 있는가(D-357).
        Bool             m_modalActive = false;
        Bool             m_modalBlocks = false;
        // 누른 자리의 커서(끌어 선택의 기준)와 이번 프레임에 떠 둔 IME 조합 글자다(D-355).
        Int32            m_fieldPressCaret = 0;
        // 더블 클릭(D-361): 입력 체인의 시계(지난 갱신 델타의 합)와 마지막으로 누른 글자 칸·때·자리.
        Float            m_clock = 0.0f;
        InstanceId       m_lastFieldPress = InvalidInstanceId;
        Float            m_lastFieldPressTime = 0.0f;
        Float            m_lastFieldPressX = 0.0f;
        Float            m_lastFieldPressY = 0.0f;
        UInt32           m_compositionLength = 0;
        UInt32           m_composition[MaxTextPerFrame] = {};
        ITextInputHost*  m_textInput = nullptr;
        // 스크롤 끌기. 누른 자리와, 누른 위젯을 품은 스크롤 뷰, 끌기가 시작됐는가, 지난 포인터 자리, 이번 프레임에 끈 양(레이어 단위).
        Float            m_pressPixelX = 0.0f;
        Float            m_pressPixelY = 0.0f;
        InstanceId       m_scrollCandidate = InvalidInstanceId;
        Bool             m_scrollDragging = false;
        Float            m_lastPixelX = 0.0f;
        Float            m_lastPixelY = 0.0f;
        Vector2          m_dragDelta;
        // 이번 프레임의 포인터다(D-360) - 갱신 훅의 툴팁 따라가기가 쓴다.
        Pointer          m_pointer;
        // 끌기 시작에 올린 오브젝트들이다(D-361). 놓을 때 이것만 내린다. 한 번에 하나만 끌린다.
        Array<Handle::GameObject> m_lifted;
        // 스크롤바의 트랙 누름(D-360): 손잡이 밖을 눌러 한 페이지씩 가는 중인가, 그 쥔 시간.
        Bool             m_sliderPaging = false;
        Bool             m_pageForward = false;
        Float            m_pageHeld = 0.0f;

        // 훅을 부를 스크립트를 먼저 모으는 자리다. 멤버로 두어 부를 때마다 할당하지 않는다.
        Array<GameScriptBase*>       m_hookTargets;
        // 글자를 고치는 자리와 붙여넣기로 읽은 클립보드다. 멤버로 두어 프레임마다 할당하지 않는다.
        String                       m_textScratch;
        String                       m_clipScratch;
        String                       m_undoScratch;
    };
}
