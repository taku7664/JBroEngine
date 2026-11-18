#pragma once

#include <JBro/Types/Bool.h>

// **묶음 안의 시험 고르기**(`JBRO_TEST_CASES`). 묶음 고르기(`JBRO_TESTS`)만으로는 `EditorApplication` 처럼 시험이 백 개 넘는 묶음을
// 고칠 때마다 통째로(5~6 분) 돌아야 했다. 묶음의 `Run…Tests` 는 시험마다 `JBRO_CASE(TestX(...));` 로 부르고, 이 값이 있으면
// 쉼표로 나눈 조각 중 하나라도 그 호출 글자(`TestX(...)`)에 들어 있는 시험만 돈다 - `JBRO_TESTS=EditorApplication JBRO_TEST_CASES=ExpandsSubAssets`.
// 비어 있으면 모두 돈다. 고른 묶음에서 하나도 걸리지 않으면 실패다(조용히 "지났다" 고 하지 않는다).
namespace TestCases
{
    JBro::Bool Wants(const char* call);
}

// 가변 인자다 - 인자에 쉼표가 있는 호출(`TestX(a, b)`)도 한 덩어리로 받는다.
#define JBRO_CASE(...)                       \
    do                                       \
    {                                        \
        if (TestCases::Wants(#__VA_ARGS__))  \
        {                                    \
            __VA_ARGS__;                     \
        }                                    \
    } while (false)
