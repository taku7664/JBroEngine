#pragma once

#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Runtime/Ref.h>

#include <cstdint>

namespace JBro::Service
{
    // 스크립트가 텍스트의 글자를 바꾸고 읽는다(D-200 (1), text-plan §4.7). 메인 스레드 전용이다.
    // 무효한 Ref 나 텍스트 시스템이 없는 자리(에디터 밖 도구 등)에서는 아무것도 하지 않고 거짓·0 을 준다.
    class Text2DService
    {
    public:
        // 글자를 바꾼다. 호스트가 바이트를 복사하므로 호출이 끝나면 버퍼를 다시 써도 된다.
        bool SetText(Ref<Component::Text2D> text, const char* utf8, std::uint32_t length) const;
        // 0 으로 끝나는 글자를 받는다.
        bool SetText(Ref<Component::Text2D> text, const char* utf8) const;
        std::uint32_t GetTextLength(Ref<Component::Text2D> text) const;
        // buffer 에 복사하고 끝에 0 을 둔다. 모자라면 자른다. 복사한 바이트 수(0 제외)다.
        std::uint32_t CopyText(Ref<Component::Text2D> text, char* buffer, std::uint32_t capacity) const;
    };
}
