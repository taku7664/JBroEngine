#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <cstddef>
#include <JBro/Types/Bool.h>

namespace JBro::Service
{
    // 스크립트가 세이브를 쓰고 읽는 표면이다(D-218). 뿌리는 사용자 앱 데이터 폴더의 `<제품명>/Saves` 이고, 에디터에서 재생한 게임은
    // `<제품명>/EditorSaves` 에 쓴다 - 만들면서 돌려 본 세이브가 실제 게임의 세이브를 덮지 않는다.
    //
    //     String text;
    //     save.ReadText("options.yaml", text);   // 없으면 거짓이고 text 는 빈 글자다
    //     save.WriteText("options.yaml", text);
    //     save.Flush();                          // 저장을 마친 뒤 한 번
    //
    // 읽은 바이트는 이 모듈 사본(게임 DLL)의 힙에 둔다. Main-thread only. 호스트의 저장소를 소유하지 않고 바인딩된 것을 부른다.
    class SaveService
    {
    public:
        Bool IsReady() const;

        Bool WriteBytes(const char* slot, const void* data, std::size_t size) const;
        Bool WriteText(const char* slot, const String& text) const;
        // 없거나 읽지 못하면 거짓이고 `out` 은 빈다.
        Bool ReadBytes(const char* slot, Array<std::byte>& out) const;
        Bool ReadText(const char* slot, String& out) const;
        Bool Exists(const char* slot) const;
        Bool Remove(const char* slot) const;
        Bool Flush() const;
    };
}
