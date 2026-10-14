#include <JBro/Types/Array.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/String.h>

#include <cstdint>

// 디버그 어댑터 시험의 스크립트 DLL 이다(D-319). 시험은 이 파일에서 `BREAK_HERE` 가 적힌 줄을 찾아 중단점을 건다 - 줄이 옮겨져도 따라간다.
// 지역 변수는 natvis 가 값으로 보여야 하는 JBro 값 타입이다. 진입점은 `extern "C"` 라 원시 `int` 다(경계).
extern "C" __declspec(dllexport) int TargetTick(int frame)
{
    JBro::Float speed = 2.5f;
    JBro::Vector2 position{JBro::Float(1.0f), JBro::Float(2.0f)};
    JBro::Array<JBro::Int32> numbers;
    numbers.Add(JBro::Int32(1));
    numbers.Add(JBro::Int32(2));
    numbers.Add(JBro::Int32(3));
    JBro::String name = "hello";
    JBro::Int32 total = JBro::Int32(static_cast<std::int32_t>(numbers.Size())) + JBro::Int32(frame); // BREAK_HERE
    total = total + JBro::Int32(static_cast<std::int32_t>(name.size())); // NEXT_LINE
    return static_cast<int>(total.Value) + static_cast<int>(speed.Value + position.x.Value);
}
