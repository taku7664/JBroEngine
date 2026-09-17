#pragma once

#include <JBro/Types/String.h>

#include <cstdint>
#include <string_view>

namespace JBro::ScriptCompiler
{
    // 원문 안의 한 자리다.
    //
    // 줄과 열은 1 부터 센다. **열은 UTF-8 바이트 단위다.** 이름은 ASCII 만 받으므로 코드의 열은
    // 글자 수와 같고, 한글은 문자열과 주석 안에만 있다. LSP 는 UTF-16 단위로 열을 세므로
    // 편집기에 넘길 때 바꿔야 한다 - 그 변환은 `jbroc --lsp` 를 만들 때 한다.
    struct SourceLocation
    {
        std::uint32_t Offset = 0;
        std::uint32_t Line = 1;
        std::uint32_t Column = 1;
    };

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
