#pragma once

#include <JBro/Canvas/GameSystem.h>
#include <JBro/Canvas/ScreenSpace.h>
#include <JBro/Framework2D/System/IScreen2DSystem.h>
#include <JBro/InputTypes/InputHandler.h>
#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Types/Array.h>

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
    class Layer;
    class GameScript2D;
}

namespace JBro::Component
{
    class Button2D;
    class ScrollView2D;
    class Slider2D;
    class Toggle2D;
    class Transform2D;
}

namespace JBro::System
{
    class ScriptSystem;

    // **UI 위젯과 화면 역투영**(D-237·D-351, ui-plan §2.5·§6.2). 입력 레이어 `"UI"` 에 선 시스템 핸들러다 - 같은 레이어의 스크립트보다 먼저 받는다.
    //
    // 한 프레임에 포인터(첫 손가락, 없으면 마우스 왼쪽 단추) 아래의 **가장 위 위젯 하나**(버튼·토글·슬라이더·스크롤 뷰)를 고른다. 화면 레이어가 월드 위이고,
    // 같은 공간에서는 레이어 차례, 그다음 같은 오브젝트의 스프라이트 `renderOrder`, 그다음 `drawSequence`(D-296)가 큰 것이 위다. 마스크(`Mask2D`) 밖의
    // 위젯은 없는 것이다. 위젯 위에 있거나 위젯을 쥐고 있으면 그 장치(마우스·터치)를 소비한다 - 아래 레이어의 핸들러와 `InputService` 의 폴링은 빈 포인터를
    // 본다. 키보드와 게임패드는 지나간다.
    //
    // 스크롤 뷰는 뷰포트 안에서 누르고 몇 픽셀 끌면 끌기가 되고, 그때 안의 버튼 누름은 취소된다. 갱신 훅(`OnUpdate`, 트랜스폼 전파보다 먼저)은
    // 값에서 그림을 놓고(슬라이더 손잡이·채우기, 토글 표시) 스크롤의 관성을 돌린다.
    //
    // 기존 엔진은 버튼이 장치를 직접 폴링했고, 게임이 `IsPointerOverButton()` 을 손으로 물어야 게임 입력이 막혔다(ui-plan §1.2 U3).
    class Button2DSystem final : public GameSystem, public IInputHandler, public IScreen2DSystem
    {
    public:
        // 끌기로 보는 거리(게임 화면 픽셀). 이보다 덜 움직이고 떼면 누름이다.
        static constexpr Float ScrollDragThreshold = 8.0f;

        Int32 GetExecutionOrder() const override;

        // 입력 체인에 선다. 없으면 위젯은 눌리지 않는다(입력이 없는 호스트).
        void SetScriptSystem(ScriptSystem* scripts);
        // 이번 프레임의 화면 기준이다. 프레임워크가 입력 체인보다 먼저 건다.
        void SetScreenSpace(const ScreenSpaceFrame& frame);

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

        enum class WidgetKind : std::uint8_t { None, Button, Toggle, Slider, Scroll };
        enum class Hook : std::uint8_t { Enter, Exit, Down, Up, Click, ValueChanged };

        static Pointer ReadPointer(InputView& input);
        // 포인터를 오브젝트의 로컬 좌표로 옮긴다. 레이어의 역투영과 월드 행렬의 역이다. `layerPoint` 를 주면 레이어 좌표도 준다.
        Bool PointerToLocal(Object::GameObject& owner, const Layer& layer, const Pointer& pointer, Vector2& local, Vector2* layerPoint = nullptr) const;
        // 누름 사각형(`offset` 가운데의 `size`, 크기는 절댓값) 안이고 마스크 안인가. 안이면 로컬 자리도 준다.
        Bool HitRect(Object::GameObject& owner, const Layer& layer, Vector2 size, Vector2 offset, const Pointer& pointer, Vector2& local) const;
        void CallHook(Object::GameObject* object, Hook hook);
        // 값에서 그림을 놓는다(슬라이더 손잡이·채우기, 토글 표시).
        void PlaceWidgets(Canvas& canvas);
        // 이 오브젝트나 조상의 스크롤 뷰(가장 가까운 것)다. 없으면 nullptr.
        Object::GameObject* FindScrollAncestor(Object::GameObject* object) const;
        // 스크롤 뷰의 내용을 옮기고 가둔다. 가둬진 축은 `clampedX`·`clampedY` 가 참이다.
        void ScrollBy(Object::GameObject& owner, Component::ScrollView2D& scroll, Vector2 delta, Bool& clampedX, Bool& clampedY);

        Canvas*          m_canvas = nullptr;
        ScriptSystem*    m_scripts = nullptr;
        ScreenSpaceFrame m_screen;
        // 오브젝트 번호로 기억한다 - 지난 프레임의 포인터가 가리키던 오브젝트가 지워졌을 수 있다. 종류는 같은 오브젝트의 어느 위젯인가다.
        InstanceId       m_hovered = InvalidInstanceId;
        WidgetKind       m_hoveredKind = WidgetKind::None;
        InstanceId       m_pressed = InvalidInstanceId;
        WidgetKind       m_pressedKind = WidgetKind::None;
        Bool             m_pointerOver = false;
        // 스크롤 끌기. 누른 자리와, 누른 위젯을 품은 스크롤 뷰, 끌기가 시작됐는가, 지난 포인터 자리, 이번 프레임에 끈 양(레이어 단위).
        Float            m_pressPixelX = 0.0f;
        Float            m_pressPixelY = 0.0f;
        InstanceId       m_scrollCandidate = InvalidInstanceId;
        Bool             m_scrollDragging = false;
        Float            m_lastPixelX = 0.0f;
        Float            m_lastPixelY = 0.0f;
        Vector2          m_dragDelta;

        // 훅을 부를 스크립트를 먼저 모으는 자리다. 멤버로 두어 부를 때마다 할당하지 않는다.
        Array<GameScript2D*>         m_hookTargets;
    };
}
