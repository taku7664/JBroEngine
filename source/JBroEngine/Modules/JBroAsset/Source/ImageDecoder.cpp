#include <JBro/Asset/ImageDecoder.h>

#include <cstring>
#include <limits>

// stb_image 의 구현은 이 번역 단위 하나에만 켠다. 형식은 넷만 남긴다 - 기존 엔진의 확장자 표와 같다.
// 파일 IO 는 끈다(D-112). 할당은 표준 것을 쓴다 - 임포트 경로라 프레임 규칙의 대상이 아니다.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include <stb_image.h>

namespace JBro
{
    bool DecodeImage(JArrayView<std::byte> encoded, DecodedImage& result)
    {
        if (encoded.data == nullptr || encoded.size == 0
            || encoded.size > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        {
            return false;
        }
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(encoded.data),
            static_cast<int>(encoded.size),
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
        decoded.width = static_cast<std::uint32_t>(width);
        decoded.height = static_cast<std::uint32_t>(height);
        decoded.pixels.Resize(byteCount);
        std::memcpy(decoded.pixels.Data(), pixels, byteCount);
        stbi_image_free(pixels);
        result = std::move(decoded);
        return true;
    }
}
