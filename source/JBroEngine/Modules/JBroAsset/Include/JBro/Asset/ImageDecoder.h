#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Types/Array.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    // 디코드된 이미지다. 픽셀은 언제나 RGBA8, 왼쪽 위가 원점, 행마다 `width * 4` 바이트다.
    struct DecodedImage
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        Array<std::byte> pixels;
    };

    // 파일 바이트를 디코드한다(`stb_image`, D-111). PNG·JPEG·BMP·TGA 를 받는다. 파일은 열지 않는다 - 바이트는 플랫폼이
    // 읽어 온다(D-112). 실패하면 `result` 를 손대지 않고 false 다. 임포트 경로의 일이고 프레임 경로가 아니다.
    bool DecodeImage(JArrayView<std::byte> encoded, DecodedImage& result);
}
