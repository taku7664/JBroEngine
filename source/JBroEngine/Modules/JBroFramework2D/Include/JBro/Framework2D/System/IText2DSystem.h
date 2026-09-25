#pragma once

#include <cstdint>

namespace JBro::Component
{
    class Text2D;
}

namespace JBro::System
{
    // 스크립트가 텍스트의 글자를 바꾸고 읽는 길이다(D-200 (1)). 구현은 호스트의 텍스트 시스템이고, 서비스는 이 인터페이스로
    // **호스트 코드를 부른다** - 스크립트 DLL 이 저장소의 문자열을 자기 할당기로 잡지 않게 하려는 것이다(D-51).
    // 입력과 출력은 POD 다(글자 포인터와 길이, 호출자가 가진 버퍼).
    class IText2DSystem
    {
    public:
        virtual ~IText2DSystem() = default;

        // 글자를 바꾼다. 호스트가 바이트를 복사한다. 다음 프레임의 레이아웃에 보인다.
        virtual void SetText(Component::Text2D& text, const char* utf8, std::uint32_t length) = 0;
        // 글자의 바이트 수다(끝의 0 은 세지 않는다).
        virtual std::uint32_t GetTextLength(const Component::Text2D& text) const = 0;
        // 글자를 buffer 에 복사하고 끝에 0 을 둔다. 모자라면 자른다. 복사한 바이트 수(0 제외)다.
        virtual std::uint32_t CopyText(const Component::Text2D& text, char* buffer, std::uint32_t capacity) const = 0;
    };
}
