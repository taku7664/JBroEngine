#pragma once

#include <JBro/ScriptCompiler/Diagnostic.h>
#include <JBro/ScriptCompiler/SourceText.h>
#include <JBro/ScriptCompiler/SyntaxTree.h>
#include <JBro/Types/UInt.h>

namespace JBro::ScriptCompiler
{
    // 식 하나의 가장 깊은 겹이다(jbroscript-syntax §22, D-374). 식 노드가 뿌리에서 잎까지 이어지는 수를 센다 -
    // `1 + 1 + 1` 은 3, `((1))` 은 3, `a ?? b ?? 0` 은 3 이다. 넘으면 `JBC2015` 로 거절한다.
    // 숫자는 생성 C++ 을 MSVC 가 받는 한도에서 왔다: `??` 사슬은 겹마다 람다 블록을 하나 열어 깊이 약 122 에서
    // C1061(블록 중첩)에 걸리고, `?.` 사슬은 320, 다른 모양은 512 까지 빌드된다. 100 이면 그 안에서 제어문 블록 40 겹이 남는다.
    inline constexpr UInt32 MaxExpressionDepth = 100;

    // 함수 하나에서 블록 `{ }` 이 겹치는 가장 깊은 수다(jbroscript-syntax §23, D-376). 함수 몸이 1 이고 `if`·`while`·`for`·`case` 의 몸이
    // 하나씩 더한다. `else if` 는 사슬이라 겹으로 세지 않는다. 넘으면 `JBC2016` 로 거절한다. MSVC 는 블록 약 128 겹에서 C1061 이고,
    // 생성 C++ 은 `??` 마다 블록을 하나 더 연다 - `??` 100 겹(식 상한)이 블록 32 겹 안에서 빌드되는 것을 시험이 본다.
    inline constexpr UInt32 MaxBlockDepth = 32;


    // 파일 하나를 구문 트리로 읽는다. 렉서도 여기서 부른다.
    //
    // 에러가 있어도 끝까지 읽는다. 에러 난 문장은 줄 끝이나 `}` 까지 건너뛰고 다음 문장부터 다시 읽는다.
    // **한 문장에서는 첫 에러만 낸다** - 괄호 하나를 빠뜨린 것이 에러 열 개가 되지 않게 한다.
    //
    // **선언인지 식인지는 모양으로 가린다.** 문장이 타입과 이름으로 시작하면(`Collision2D hit`,
    // `Array<ref Enemy> allies`, `ref Enemy nearest`) 선언이다. 이 언어의 식에는 이름 두 개가 나란히 오는 모양이
    // 없기 때문이다. 그래서 타입 이름을 미리 알 필요가 없고, 파일 하나만으로 읽을 수 있다.
    // `GetComponent<Transform2D>()` 처럼 `<` 뒤가 타입 인자로 읽히고 `>` 바로 뒤에 `(` 가 오면 제네릭 호출이다.
    //
    // 문맥 키워드(`callback`·`override`·`require` 는 함수 선언 끝, `in` 은 `for` 괄호 안)는 그 자리에서만 키워드로 본다.
    SyntaxTree Parse(const SourceText& source, DiagnosticList& diagnostics);
}
