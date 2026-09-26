#pragma once

#include <JBro/Framework3D/Component/Text3D.h>
#include <JBro/Runtime/TextService.h>

namespace JBro::Service
{
    // 스크립트가 3D 텍스트의 글자를 바꾸고 읽는다(D-223). `Text2DService` 와 같은 공용 몸통이고, 3D 시스템 컨텍스트의 텍스트
    // 슬롯을 찾는 길만 다르다. 메인 스레드 전용이다.
    class Text3DService : public TextServiceBase<Component::Text3D, Text3DService>
    {
    public:
        static System::ITextSystem* GetTextSystem();
    };
}
