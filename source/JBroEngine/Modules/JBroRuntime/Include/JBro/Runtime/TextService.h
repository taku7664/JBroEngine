#pragma once

#include <JBro/Runtime/ITextSystem.h>
#include <JBro/Runtime/Ref.h>

#include <cstdint>
#include <cstring>

namespace JBro::Service
{
    // 텍스트 서비스의 공용 부분이다(D-223). 차원별 서비스(`Text2DService`·`Text3DService`)가 물려받고, 제 컴포넌트 타입과
    // 제 시스템을 찾는 길(`TDerived::GetTextSystem()`, 차원별 시스템 컨텍스트의 슬롯)만 준다. 컴포넌트는 `text`(TextId) 필드를 든다.
    //
    // 가상 함수를 두지 않는다 - 서비스는 서비스 컨텍스트(POD) 안에 값으로 들어가 DLL 경계를 넘는다. 메인 스레드 전용이다.
    // 무효한 Ref 나 텍스트 시스템이 없는 자리(에디터 밖 도구 등)에서는 아무것도 하지 않고 거짓·0 을 준다.
    template <typename TComponent, typename TDerived>
    class TextServiceBase
    {
    public:
        // 글자를 바꾼다. 호스트가 바이트를 복사하므로 호출이 끝나면 버퍼를 다시 써도 된다.
        bool SetText(Ref<TComponent> text, const char* utf8, std::uint32_t length) const
        {
            System::ITextSystem* system = TDerived::GetTextSystem();
            TComponent* component = text.Get();
            if (system == nullptr || component == nullptr || (utf8 == nullptr && length != 0))
            {
                return false;
            }
            system->SetText(component->text, utf8, length);
            return true;
        }

        // 0 으로 끝나는 글자를 받는다.
        bool SetText(Ref<TComponent> text, const char* utf8) const
        {
            const std::size_t length = utf8 != nullptr ? std::strlen(utf8) : 0;
            if (length > 0xFFFFFFFFu)
            {
                return false;
            }
            return SetText(text, utf8, static_cast<std::uint32_t>(length));
        }

        std::uint32_t GetTextLength(Ref<TComponent> text) const
        {
            System::ITextSystem* system = TDerived::GetTextSystem();
            const TComponent* component = text.Get();
            if (system == nullptr || component == nullptr)
            {
                return 0;
            }
            return system->GetTextLength(component->text);
        }

        // buffer 에 복사하고 끝에 0 을 둔다. 모자라면 자른다. 복사한 바이트 수(0 제외)다.
        std::uint32_t CopyText(Ref<TComponent> text, char* buffer, std::uint32_t capacity) const
        {
            if (buffer == nullptr || capacity == 0)
            {
                return 0;
            }
            System::ITextSystem* system = TDerived::GetTextSystem();
            const TComponent* component = text.Get();
            if (system == nullptr || component == nullptr)
            {
                buffer[0] = '\0';
                return 0;
            }
            return system->CopyText(component->text, buffer, capacity);
        }
    };
}
