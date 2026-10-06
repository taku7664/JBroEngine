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
    class GameObject;
    class Layer;
    class GameScript2D;
}

namespace JBro::Component
{
    class Button2D;
}

namespace JBro::System
{
    class ScriptSystem;

    // **버튼과 화면 역투영**(D-237, ui-plan §2.5). 입력 레이어 `"UI"` 에 선 시스템 핸들러다 - 같은 레이어의 스크립트보다 먼저 받는다.
    //
    // 한 프레임에 포인터(첫 손가락, 없으면 마우스 왼쪽 단추) 아래의 **가장 위 버튼 하나**를 고른다. 화면 레이어가 월드 위이고, 같은 공간에서는
    // 레이어 차례, 그다음 같은 오브젝트의 스프라이트 `renderOrder`, 그다음 `drawSequence`(D-296)가 큰 것이 위다. 버튼 위에 있거나 버튼을 누르고 있으면 그 장치(마우스·터치)를
    // 소비한다 - 아래 레이어의 핸들러와 `InputService` 의 폴링은 빈 포인터를 본다. 키보드와 게임패드는 지나간다.
    //
    // 기존 엔진은 버튼이 장치를 직접 폴링했고, 게임이 `IsPointerOverButton()` 을 손으로 물어야 게임 입력이 막혔다(ui-plan §1.2 U3).
    class Button2DSystem final : public GameSystem, public IInputHandler, public IScreen2DSystem
    {
    public:
        Int32 GetExecutionOrder() const override;

        // 입력 체인에 선다. 없으면 버튼은 눌리지 않는다(입력이 없는 호스트).
        void SetScriptSystem(ScriptSystem* scripts);
        // 이번 프레임의 화면 기준이다. 프레임워크가 입력 체인보다 먼저 건다.
        void SetScreenSpace(const ScreenSpaceFrame& frame);

        InputResult OnInput(InputView& input) override;

        Bool ScreenToLayer(Vector2 pixel, GameObjectHandle object, Vector2& point) const override;
        Bool LayerToScreen(Vector2 point, GameObjectHandle object, Vector2& pixel) const override;
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

        enum class Hook : std::uint8_t { Enter, Exit, Down, Up, Click };

        static Pointer ReadPointer(InputView& input);
        Bool HitTest(const Component::Button2D& button, GameObject& owner, const Layer& layer, const Pointer& pointer) const;
        void CallHook(GameObject* object, Hook hook);
        void RefreshScriptKeys();

        Canvas*          m_canvas = nullptr;
        ScriptSystem*    m_scripts = nullptr;
        ScreenSpaceFrame m_screen;
        // 오브젝트 번호로 기억한다 - 지난 프레임의 포인터가 가리키던 오브젝트가 지워졌을 수 있다.
        InstanceId       m_hovered = InvalidInstanceId;
        InstanceId       m_pressed = InvalidInstanceId;
        Bool             m_pointerOver = false;

        // 훅을 부를 스크립트를 가른다(물리와 같은 방식, D-207). 스크립트 순서가 바뀐 때만 다시 모은다.
        Array<GameScriptBase*>       m_collectedScripts;
        Array<const ComponentBase*>  m_scriptKeys;
        UInt64                m_scriptRevision = ~UInt64{0};
        Array<GameScript2D*>         m_hookTargets;
    };
}
