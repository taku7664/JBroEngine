#include <JBro/Types/Array.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/String.h>

#include <cstdint>

// 디버그 어댑터 시험의 스크립트 DLL 이다(D-330 3 단계). 시험은 이 파일에서 `BREAK_HERE` 가 적힌 줄을 찾아 중단점을 건다 - 줄이 옮겨져도 따라간다.
// 지역 변수는 natvis 가 값으로 보여야 하는 JBro 값 타입이다. 진입점은 `extern "C"` 라 원시 타입이다(경계).
//
// 에디터 안에서는 엔진이 int 3 으로 그 줄에 서지만, 시험의 대상 프로그램은 엔진이 아니다. 그래서 그 줄에서 호스트가 준 콜백을 부른다 -
// 호스트가 그 자리에서 자기 문맥을 떠 어댑터에게 `stopped` 로 보낸다. 어댑터가 그 문맥으로 스택을 읽으면 이 함수가 보인다.
namespace
{
    void (*g_break)() = nullptr;
}

extern "C" __declspec(dllexport) void TargetSetBreakCallback(void (*callback)())
{
    g_break = callback;
}

extern "C" __declspec(dllexport) int TargetTick(int frame)
{
    JBro::Float speed = 2.5f;
    JBro::Vector2 position{JBro::Float(1.0f), JBro::Float(2.0f)};
    JBro::Array<JBro::Int32> numbers;
    numbers.Add(JBro::Int32(1));
    numbers.Add(JBro::Int32(2));
    numbers.Add(JBro::Int32(3));
    JBro::String name = "hello";
    JBro::Int32 total = JBro::Int32(static_cast<std::int32_t>(numbers.Size())) + JBro::Int32(frame);
    if (g_break != nullptr) { g_break(); } // BREAK_HERE
    total = total + JBro::Int32(static_cast<std::int32_t>(name.size()));
    return static_cast<int>(total.Value) + static_cast<int>(speed.Value + position.x.Value);
}
