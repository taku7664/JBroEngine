#pragma once

#include <JBro/Runtime/ITextSystem.h>

#include <cstdint>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // `ITextSystem` 의 호스트 구현이다(D-224). 2D·3D 텍스트 시스템이 물려받는다 - 글자는 호스트의 `TextStore` 에 있고 두 차원이
    // 같은 저장소를 쓰므로 구현도 하나다. 호스트 코드에서만 돈다(스크립트는 서비스로 이 가상 함수를 부른다).
    class TextSystemBase : public System::ITextSystem
    {
    public:
        void SetText(TextId& text, const char* utf8, UInt32 length) override;
        UInt32 GetTextLength(const TextId& text) const override;
        UInt32 CopyText(const TextId& text, char* buffer, UInt32 capacity) const override;
    };
}
