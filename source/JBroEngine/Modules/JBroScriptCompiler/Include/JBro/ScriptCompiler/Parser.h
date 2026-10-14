#pragma once

#include <JBro/ScriptCompiler/Diagnostic.h>
#include <JBro/ScriptCompiler/SourceText.h>
#include <JBro/ScriptCompiler/SyntaxTree.h>

namespace JBro::ScriptCompiler
{
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
