#pragma once

#include <JBro/Runtime/TextStore.h>

#include <cstdint>

namespace JBro::System
{
    // 스크립트가 텍스트의 글자를 바꾸고 읽는 길이다(D-200 (1), D-223). 차원과 무관하다 - 2D·3D 텍스트 시스템이 같은 구현을 물려받는다
    // (`JBroTextRendering` 의 `TextSystemBase`). 서비스는 이 인터페이스로 **호스트 코드를 부른다** - 스크립트 DLL 이 저장소의 문자열을
    // 자기 할당기로 잡지 않게 하려는 것이다(D-51). 입력과 출력은 POD 다(번호, 글자 포인터와 길이, 호출자가 가진 버퍼).
    //
    // 가상 함수 표는 스크립트 DLL 과의 ABI 다. 바꾸면 이것을 드는 차원별 시스템 컨텍스트의 판번호를 올린다(D-28).
    class ITextSystem
    {
    public:
        virtual ~ITextSystem() = default;

        // 글자를 바꾼다. 호스트가 바이트를 복사한다. 다음 프레임의 레이아웃에 보인다.
        virtual void SetText(TextId& text, const char* utf8, std::uint32_t length) = 0;
        // 글자의 바이트 수다(끝의 0 은 세지 않는다).
        virtual std::uint32_t GetTextLength(const TextId& text) const = 0;
        // 글자를 buffer 에 복사하고 끝에 0 을 둔다. 모자라면 자른다. 복사한 바이트 수(0 제외)다.
        virtual std::uint32_t CopyText(const TextId& text, char* buffer, std::uint32_t capacity) const = 0;
    };
}
