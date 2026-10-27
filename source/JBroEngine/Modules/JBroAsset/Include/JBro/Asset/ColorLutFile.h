#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>

#include <cstddef>

namespace JBro
{
    // **3D LUT 하나**(D-343, `.cube`). `rgb` 는 `size`³ 칸의 색(칸마다 셋)이고 차례는 파일과 같다 - 빨강이 가장 빨리, 파랑이 가장 늦게 돈다.
    // 정의역은 입력 값의 범위다(기본 0..1). 컬러 그레이딩이 입력을 이 범위로 맞춰 LUT 를 읽는다.
    struct ColorLut
    {
        UInt32 size = 0;
        Float domainMin[3] = {0.0f, 0.0f, 0.0f};
        Float domainMax[3] = {1.0f, 1.0f, 1.0f};
        Array<Float> rgb;
        String title;
    };

    // `.cube`(Adobe·Resolve 의 LUT 글자 형식)를 읽는다. 받는 키는 `TITLE`·`LUT_3D_SIZE`·`DOMAIN_MIN`·`DOMAIN_MAX` 이고 `#` 뒤는 주석이다.
    // 1D LUT(`LUT_1D_SIZE`)·크기 2 미만이나 256 초과·칸 수가 맞지 않음·정의역이 뒤집힘·숫자가 아닌 줄은 거짓이고 `error` 에 줄과 까닭을 적는다.
    Bool ParseColorLut(const char* text, std::size_t length, ColorLut& lut, String* error = nullptr);

    // 반정밀 실수 RGBA 텍셀(칸마다 8 바이트, 알파 1)로 옮긴다. 렌더러의 3D 텍스처(`Renderer::RegisterVolumeTexture`)가 받는 모양이다.
    void PackColorLutRgba16(const ColorLut& lut, Array<std::byte>& texels);
}
