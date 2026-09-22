#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/String.h>

#include <cstdint>

namespace JBro::Widget
{
    // 에셋을 끌어 옮기는 꾸러미다(D-154, 기존 `EditorDragDrop`).
    //
    // 에셋 브라우저에서 끌어 **폴더에 놓으면 옮기고**(D-139), **에셋 칸에 놓으면 그 에셋을
    // 고른다**. 한 번 끄는 데 꾸러미는 하나라서, 두 받는 쪽이 저마다 필요한 것을 한 꾸러미에 담는다:
    // 폴더는 상대경로들을, 칸은 아이디를 본다.
    //
    // 아이디가 **둘**인 것은 이미지 때문이다. 그림 파일은 Texture 와 Sprite 두 레코드로 서고
    // 목록의 한 줄은 Texture 를 가리키는데, 스프라이트 칸이 받아야 하는 것은 Sprite 쪽이다 -
    // 짝을 함께 실어 두면 칸이 자기 목록에 있는 쪽을 고른다.
    inline constexpr const char* AssetDragPayloadType = "JBRO_ASSET";

    struct AssetDragHeader
    {
        AssetId primary;
        // 짝 레코드다(그림의 Sprite). 없으면 비어 있다.
        AssetId paired;
        // 뒤따르는 상대경로 묶음(줄바꿈으로 갈림, 끝에 0)의 바이트 수다.
        std::uint32_t pathBytes = 0;
    };

    // 끌기 시작에서 부른다. `paths` 는 줄바꿈으로 갈린 상대경로 묶음이다.
    void SetAssetDragPayload(AssetId primary, AssetId paired, const String& paths);
    // 드롭 대상 안(`BeginDragDropTarget` 이 참일 때)에서 부른다. 놓였으면 참이다.
    // `paths` 를 주면 상대경로 묶음도 준다.
    bool AcceptAssetDrop(AssetDragHeader& header, String* paths = nullptr);
}
