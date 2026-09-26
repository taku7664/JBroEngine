#pragma once

#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Runtime/TextService.h>

namespace JBro::Service
{
    // 스크립트가 2D 텍스트의 글자를 바꾸고 읽는다(D-200 (1), text-plan §4.7). 몸통은 공용 `TextServiceBase` 이고(D-223), 여기는
    // 2D 시스템 컨텍스트의 텍스트 슬롯을 찾는 길만 준다. 메인 스레드 전용이다.
    class Text2DService : public TextServiceBase<Component::Text2D, Text2DService>
    {
    public:
        static System::ITextSystem* GetTextSystem();
    };
}
