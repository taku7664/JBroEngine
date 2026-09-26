#pragma once

#include <JBro/Runtime/ITextSystem.h>

#include <cstdint>

namespace JBro
{
    // `ITextSystem` 의 호스트 구현이다(D-223). 2D·3D 텍스트 시스템이 물려받는다 - 글자는 호스트의 `TextStore` 에 있고 두 차원이
    // 같은 저장소를 쓰므로 구현도 하나다. 호스트 코드에서만 돈다(스크립트는 서비스로 이 가상 함수를 부른다).
    class TextSystemBase : public System::ITextSystem
    {
    public:
        void SetText(TextId& text, const char* utf8, std::uint32_t length) override;
        std::uint32_t GetTextLength(const TextId& text) const override;
        std::uint32_t CopyText(const TextId& text, char* buffer, std::uint32_t capacity) const override;
    };
}
