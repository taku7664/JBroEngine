#pragma once

#include <JBro/Runtime/ITextSystem.h>
#include <JBro/Runtime/TextStore.h>

#include <cstdint>
#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::Service
{
    // 텍스트 서비스의 공용 부분이다(D-224). 차원별 서비스(`Text2DService`·`Text3DService`)가 물려받고, 제 핸들 타입과
    // 제 시스템을 찾는 길(`TDerived::GetTextSystem()`, 차원별 시스템 컨텍스트의 슬롯), 핸들에서 글자 칸을 찾는 길(`TDerived::FindTextField`)만 준다.
    // 스크립트는 컴포넌트 데이터를 보지 못하므로(D-302) 글자 칸은 데이터 헤더를 보는 차원별 `.cpp` 가 찾는다.
    //
    // 가상 함수를 두지 않는다 - 서비스는 서비스 컨텍스트(POD) 안에 값으로 들어가 DLL 경계를 넘는다. 메인 스레드 전용이다.
    // 무효한 핸들이나 텍스트 시스템이 없는 자리(에디터 밖 도구 등)에서는 아무것도 하지 않고 거짓·0 을 준다.
    template <typename THandle, typename TDerived>
    class TextServiceBase
    {
    public:
        // 글자를 바꾼다. 호스트가 바이트를 복사하므로 호출이 끝나면 버퍼를 다시 써도 된다.
        Bool SetText(THandle text, const char* utf8, UInt32 length) const
        {
            System::ITextSystem* system = TDerived::GetTextSystem();
            TextId* field = TDerived::FindTextField(text, false);
            if (system == nullptr || field == nullptr || (utf8 == nullptr && length != 0))
            {
                return false;
            }
            system->SetText(*field, utf8, length);
            return true;
        }

        // 0 으로 끝나는 글자를 받는다.
        Bool SetText(THandle text, const char* utf8) const
        {
            const std::size_t length = utf8 != nullptr ? std::strlen(utf8) : 0;
            if (length > 0xFFFFFFFFu)
            {
                return false;
            }
            return SetText(text, utf8, static_cast<JBro::UInt32>(length));
        }

        // 게임 문자열 표의 키를 바꾼다(D-226). 빈 키를 주면 `text` 의 글자로 돌아간다. 글자 칸은 `text` 와 같은 길로 호스트가 복사한다.
        Bool SetTextKey(THandle text, const char* key) const
        {
            System::ITextSystem* system = TDerived::GetTextSystem();
            TextId* field = TDerived::FindTextField(text, true);
            const std::size_t length = key != nullptr ? std::strlen(key) : 0;
            if (system == nullptr || field == nullptr || length > 0xFFFFFFFFu)
            {
                return false;
            }
            system->SetText(*field, key, static_cast<JBro::UInt32>(length));
            return true;
        }

        UInt32 GetTextLength(THandle text) const
        {
            System::ITextSystem* system = TDerived::GetTextSystem();
            const TextId* field = TDerived::FindTextField(text, false);
            if (system == nullptr || field == nullptr)
            {
                return 0;
            }
            return system->GetTextLength(*field);
        }

        // buffer 에 복사하고 끝에 0 을 둔다. 모자라면 자른다. 복사한 바이트 수(0 제외)다.
        UInt32 CopyText(THandle text, char* buffer, UInt32 capacity) const
        {
            if (buffer == nullptr || capacity == 0)
            {
                return 0;
            }
            System::ITextSystem* system = TDerived::GetTextSystem();
            const TextId* field = TDerived::FindTextField(text, false);
            if (system == nullptr || field == nullptr)
            {
                buffer[0] = '\0';
                return 0;
            }
            return system->CopyText(*field, buffer, capacity);
        }
    };
}
