#pragma once

#include <JBro/Types/String.h>

#include <cstdint>
#include <string_view>
#include <JBro/Types/UInt.h>

namespace JBro::ScriptCompiler
{
    // 원문 안의 한 자리다.
    //
    // 줄과 열은 1 부터 센다. **열은 UTF-8 바이트 단위다.** 이름은 ASCII 만 받으므로 코드의 열은
    // 글자 수와 같고, 한글은 문자열과 주석 안에만 있다. LSP 는 UTF-16 단위로 열을 세므로
    // 편집기에 넘길 때 바꿔야 한다 - 그 변환은 `jbroc --lsp` 를 만들 때 한다.
    struct SourceLocation
    {
        UInt32 Offset = 0;
        UInt32 Line = 1;
        UInt32 Column = 1;
    };

    // 사용자에게 보이는 열(1 부터)이다 - 줄 시작부터 그 자리까지의 **글자(코드 포인트) 수 + 1**. `jbroc` 명령줄의 진단 줄이 쓴다(D-379):
    // `$msCompile` 매처와 에디터 `빌드 결과` 는 열을 글자로 읽으므로 바이트 열을 내면 한글 뒤에서 어긋났다. `text` 는 그 자리가 든 원문 전체다.
    UInt32 CharacterColumn(std::string_view text, const SourceLocation& at) noexcept;

    // [Begin, End) 다. End 는 마지막 글자의 다음 자리다.
    struct SourceRange
    {
        SourceLocation Begin;
        SourceLocation End;
    };

    // 컴파일할 파일 하나다. 토큰과 구문 트리는 이 글을 가리키는 `std::string_view` 를 들고 있으므로
    // **이것이 그들보다 오래 살아야 한다.**
    class SourceText final
    {
    public:
        SourceText(String path, String text);

        const String& GetPath() const noexcept { return m_path; }
        std::string_view GetText() const noexcept { return m_text.View(); }

    private:
        String m_path;
        String m_text;
    };
}
