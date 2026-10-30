#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/String.h>

#include <cstddef>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // **렌더 텍스처의 본문**(D-362, `.jrt`). 폭·높이(픽셀, 1..4096)와 샘플러다. 카메라가 이 크기의 텍스처에 그리고 스프라이트가 그것을 보인다.
    struct RenderTextureSettings
    {
        static constexpr UInt32 MaxSide = 4096;
        UInt32 width = 256;
        UInt32 height = 256;
        // `Default` 는 받지 않는다 - 읽을 때 `Linear` 로 둔다.
        TextureFilter filter = TextureFilter::Linear;
    };

    // 글자를 읽는다. 빈 글자·없는 키는 기본값이고, 범위 밖의 크기는 1..MaxSide 로 가둔다. YAML 이 깨졌으면 거짓이고 `error` 가 까닭이다.
    Bool ParseRenderTexture(const char* text, std::size_t length, RenderTextureSettings& settings, String* error = nullptr);
    String WriteRenderTexture(const RenderTextureSettings& settings);
}
