#pragma once

#include <JBro/Types/String.h>

#include <Windows.h>

#include <string>

namespace JBro::DebugAdapter
{
    // DAP 는 UTF-8 이고 DbgEng 의 `Wide` API 는 UTF-16 이다. 좁은 API 는 시스템 코드 페이지라 한글 경로가 깨진다 - 쓰지 않는다.
    inline std::wstring ToWide(const String& text)
    {
        if (text.empty())
        {
            return std::wstring();
        }
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
        return wide;
    }

    inline String ToUtf8(const wchar_t* text)
    {
        if (text == nullptr || text[0] == L'\0')
        {
            return String();
        }
        const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        String narrow(static_cast<std::size_t>(length > 0 ? length - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text, -1, narrow.data(), length, nullptr, nullptr);
        return narrow;
    }
}
