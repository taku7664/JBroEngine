#include <JBro/Asset/ImageDecoder.h>

#include <cstring>
#include <limits>

// stb_image 의 구현은 이 번역 단위 하나에만 켠다. 형식은 기존 엔진의 확장자 표의 넷과 하늘 맵의 HDR(D-447)이다.
// 파일 IO 는 끈다(D-112). 할당은 표준 것을 쓴다 - 임포트 경로라 프레임 규칙의 대상이 아니다.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_HDR
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include <stb_image.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    Bool DecodeImage(JArrayView<std::byte> encoded, DecodedImage& result)
    {
        if (encoded.data == nullptr || encoded.size == 0
            || encoded.size > static_cast<JBro::UInt32>(std::numeric_limits<int>::max()))
        {
            return false;
        }
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(encoded.data),
            static_cast<JBro::Int32>(encoded.size),
            &width,
            &height,
            &channels,
            STBI_rgb_alpha);
        if (pixels == nullptr || width <= 0 || height <= 0)
        {
            if (pixels != nullptr)
            {
                stbi_image_free(pixels);
            }
            return false;
        }
        const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
        DecodedImage decoded;
        decoded.width = static_cast<JBro::UInt32>(width);
        decoded.height = static_cast<JBro::UInt32>(height);
        decoded.pixels.Resize(byteCount);
        std::memcpy(decoded.pixels.Data(), pixels, byteCount);
        stbi_image_free(pixels);
        result = std::move(decoded);
        return true;
    }

    Bool DecodeHdrImage(JArrayView<std::byte> encoded, DecodedHdrImage& result)
    {
        if (encoded.data == nullptr || encoded.size == 0
            || encoded.size > static_cast<JBro::UInt32>(std::numeric_limits<int>::max()))
        {
            return false;
        }
        const stbi_uc* bytes = reinterpret_cast<const stbi_uc*>(encoded.data);
        const int length = static_cast<JBro::Int32>(encoded.size);
        // HDR 이 아닌 그림(PNG 등)은 받지 않는다 - stb 는 8 비트 그림도 실수로 올려 주지만 하늘 맵의 빛이 아니다.
        if (stbi_is_hdr_from_memory(bytes, length) == 0)
        {
            return false;
        }
        // stb 는 HDR 을 그대로 둔다(감마를 걸지 않는다) - 8 비트 그림에만 감마가 걸린다.
        int width = 0;
        int height = 0;
        int channels = 0;
        float* pixels = stbi_loadf_from_memory(bytes, length, &width, &height, &channels, STBI_rgb);
        if (pixels == nullptr || width <= 0 || height <= 0)
        {
            if (pixels != nullptr)
            {
                stbi_image_free(pixels);
            }
            return false;
        }
        const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u;
        DecodedHdrImage decoded;
        decoded.width = static_cast<JBro::UInt32>(width);
        decoded.height = static_cast<JBro::UInt32>(height);
        decoded.rgb.Resize(count);
        for (std::size_t at = 0; at < count; ++at)
        {
            decoded.rgb[at] = pixels[at];
        }
        stbi_image_free(pixels);
        result = std::move(decoded);
        return true;
    }
}
