#pragma once

#include <JBro/InputTypes/InputView.h>
#include <JBro/Types/NameTable.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace JBro
{
    // 핸들러가 돌려주는 값이다(D-201).
    enum class InputResult : std::uint8_t
    {
        // 아래 레이어도 받는다. 기본이다.
        Pass,
        // 아래 핸들러를 부르지 않고, `OnUpdate` 의 폴링에도 빈 입력만 남긴다.
        Block
    };

    // 레이어 체인에 서는 것이다. 스크립트는 이것을 직접 상속하지 않고 `InputHandler<Layer, Order>` 를 상속한다.
    //
    // 부르는 쪽은 호스트다. 켜진 스크립트만, 시작 훅을 받은 뒤에, 그 프레임의 `OnFixedUpdate`·`OnUpdate` 보다 먼저 부른다.
    class IInputHandler
    {
    public:
        // 매 프레임 한 번 부른다(입력이 없는 프레임도). `input` 은 그 프레임 안에서만 읽는다.
        virtual InputResult OnInput(InputView& input) = 0;

    protected:
        ~IInputHandler() = default;
    };

    // 문자열을 템플릿 인자로 받는 껍데기다(C++20). `InputHandler<"UI", 10>` 처럼 쓴다.
    template<std::size_t Length>
    struct InputLayerName
    {
        char value[Length] = {};

        constexpr InputLayerName(const char (&text)[Length])
        {
            std::copy_n(text, Length, value);
        }
    };

    // 레이어와 순서를 상속 줄에 적는 믹스인이다(D-201). 기존 엔진과 같은 모양이다.
    //
    //   class Pause final : public GameScript2D, public InputHandler<"UI", 10>
    //   {
    //       InputResult OnInput(InputView& input) override { ... }
    //   };
    //
    // **등록하지 않는다.** 엔진이 스크립트 타입에서 이 믹스인을 컴파일 타임에 알아보고 체인에 세운다.
    // 레이어 순서는 프로젝트가 정하고(위가 먼저), 같은 레이어에서는 `Order` 가 큰 것이 먼저다.
    // 없는 레이어 이름은 맨 아래로 가고 한 번 경고가 남는다.
    template<InputLayerName Layer = InputLayerName("Game"), int Order = 0>
    class InputHandler : public IInputHandler
    {
    public:
        static constexpr const char* InputLayerText = Layer.value;
        static constexpr NameId InputLayerId = MakeNameId(Layer.value);
        static constexpr std::int32_t InputOrder = Order;

    protected:
        ~InputHandler() = default;
    };
}
