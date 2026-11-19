#pragma once

#include <JBro/Canvas/ScreenSpace.h>
#include <JBro/InputTypes/InputHandler.h>
#include <JBro/Types/Math2D.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro
{
    class GameScriptBase;
    class Layer;
}

namespace JBro::System
{
    // **화면 UI 가 프레임워크에 기대는 셋**(D-362). 화면 UI 모듈(`JBroScreenUi`)은 2D·3D 가 같이 쓰므로 차원의 것을 모른다 - 스크립트 타입, 월드 레이어의
    // 카메라, 입력 체인은 프레임워크가 넣어 준다. 없으면 그 기능이 없다(훅을 부르지 않는다·월드 레이어의 위젯은 누를 수 없다·체인에 서지 않는다).

    // 위젯·목록이 스크립트에 알리는 훅이다. 이름은 `GetUiHookName`(`OnClick` 따위).
    enum class UiHook : std::uint8_t
    {
        PointerEnter,
        PointerExit,
        PointerDown,
        PointerUp,
        Click,
        ValueChanged,
        Submit,
        DragBegin,
        DragEnd,
        Drop,
        ListItem,
        ListMeasure,
    };

    const char* GetUiHookName(UiHook hook);

    // 스크립트 하나에 훅 하나를 부른다. 2D 는 `GameScript2D` 로 부른다. 부르는 쪽이 `ScriptCallGuard` 로 감싼다.
    class IUiHookDispatcher
    {
    public:
        virtual ~IUiHookDispatcher() = default;
        virtual void Call(GameScriptBase& script, UiHook hook) = 0;
    };

    // 월드 레이어의 픽셀 ↔ 레이어 좌표다. 화면 레이어는 화면 UI 가 직접 잰다. `screen` 은 이번 프레임의 화면 기준이다.
    class IWorldLayerMapper
    {
    public:
        virtual ~IWorldLayerMapper() = default;
        virtual Bool PixelToLayer(const Layer& layer, const ScreenSpaceFrame& screen, Float pixelX, Float pixelY, Vector2& point) const = 0;
        virtual Bool LayerToPixel(const Layer& layer, const ScreenSpaceFrame& screen, Vector2 point, Float& pixelX, Float& pixelY) const = 0;
    };

    // 입력 체인이다. 위젯 시스템이 `"UI"` 레이어에 선다. 2D 는 스크립트 시스템이 체인을 든다.
    class IUiInputChain
    {
    public:
        virtual ~IUiInputChain() = default;
        virtual void AddSystemInputHandler(IInputHandler& handler, const char* layer, Int32 order) = 0;
        virtual void RemoveSystemInputHandler(IInputHandler& handler) = 0;
    };
}
