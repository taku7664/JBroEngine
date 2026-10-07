#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/String.h>

#include <cstddef>
#include <JBro/Types/Bool.h>

namespace JBro
{
    // `.jfx` 본문을 읽고 쓴다(D-306). 모양은 기존 엔진과 같다:
    //
    //   Effect:
    //     Kind: Reverb
    //     Parameters:
    //       roomSize: 0.5
    //
    // 바깥 `Effect:` 키는 읽을 때 없어도 된다(기존 로더와 같다). 모르는 종류는 Reverb, 모르는 키는 버리고, 빠진 키는 그 종류의
    // 기본값이며, 값은 그 파라미터의 범위로 자른다 - 기존은 에디터만 범위를 알아 손으로 고친 파일의 값이 그대로 DSP 에 갔다.
    // 빈 글자는 Reverb 기본값으로 읽힌다(새로 만든 파일). YAML 이 깨졌으면 거짓이고 `error` 에 줄과 까닭을 적는다.
    Bool ParseAudioEffect(const char* text, std::size_t length, AudioEffectSettings& settings, String* error = nullptr);

    // 그 종류의 파라미터를 표의 차례로 적는다. 부동소수는 되읽어 같은 값이 나오는 가장 짧은 글자다.
    String WriteAudioEffect(const AudioEffectSettings& settings);
}
