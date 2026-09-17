#pragma once

#include <JBro/ScriptCompiler/Diagnostic.h>
#include <JBro/ScriptCompiler/SourceText.h>
#include <JBro/ScriptCompiler/Token.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <string_view>

namespace JBro::ScriptCompiler
{
    // 원문을 토큰으로 자른다.
    //
    // 결과는 언제나 `EndOfFile` 로 끝난다. 에러가 있어도 끝까지 자른다 - 편집기는 한 파일에서
    // 에러를 모두 보여야 하고, 파서는 에러 난 자리를 건너뛰고 계속 읽는다.
    //
    // 규칙(jbroscript-syntax §2, D-104):
    // - 줄바꿈은 문장 끝이다. 연속된 줄바꿈은 하나로 모은다. 파일 맨 앞의 줄바꿈은 내지 않는다.
    // - **`( )` 와 `[ ]` 안의 줄바꿈은 내지 않는다.** `{` 나 `}` 를 만나면 괄호 깊이를 0 으로 되돌린다.
    //   이 언어는 괄호 안에 중괄호가 오지 않으므로, 닫지 않은 괄호가 파일 끝까지 줄바꿈을 먹지 않게 한다.
    // - 주석은 `//` 부터 줄 끝까지다.
    // - 숫자는 `20` 과 `0.5` 두 모양이다. `0..n` 은 정수 `0` 과 `..` 이다. 숫자 바로 뒤에 글자가
    //   붙으면(`10f`, `2D`) 에러다. 정수는 64비트에 들어가야 한다.
    // - 문자열의 이스케이프는 `\"` `\\` `\n` `\t` 뿐이다. 줄이 바뀌기 전에 닫혀야 한다.
    // - 이름은 ASCII 글자·숫자·`_` 다. C++ 이름으로 1:1 로 내리기 때문이다(jbroc-rules §5.1).
    // - `&&` `||` `!` 는 `and` `or` `not` 을 쓰라는 에러다.
    // - 파일 맨 앞의 UTF-8 BOM 은 건너뛴다.
    Array<Token> Lex(const SourceText& source, DiagnosticList& diagnostics);

    // 문자열 리터럴 토큰의 글을 풀어 낸다. 따옴표를 벗기고 이스케이프를 바꾼다.
    // 렉서가 이미 검사했으므로 모르는 이스케이프는 그대로 둔다.
    String DecodeStringLiteral(std::string_view literal);
}
