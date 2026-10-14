#pragma once

#include <JBro/AssetTypes/AssetTypes.h>

#include <JBro/Types/Float.h>

namespace JBro
{
    // **편집 창의 그래프가 읽는 이펙트의 모양이다**(D-309). 소리를 내는 DSP(`AudioMixer.cpp`)와 같은 계수·같은 식을 쓴다 - 그림을 따로 지으면
    // 소리와 그림이 따로 논다. 메인 스레드에서 부르고, 상태(지연 줄·포락선)는 들지 않는 정적 모양만 준다.

    // 필터(`LowPass`·`HighPass`)와 `Equalizer` 의 주파수 응답이다(dB, 0 이면 그대로). 그 밖의 종류는 0 이다.
    Float AudioEffectResponseDb(const AudioEffectSettings& effect, Float hz, Float sampleRate);

    // `Distortion` 의 입출력 곡선이다. 입력 -1..1 의 원음과 일그러진 소리를 `wet` 으로 섞은 값이다. 그 밖의 종류는 입력 그대로다.
    Float AudioEffectShape(const AudioEffectSettings& effect, Float input);

    // `Compressor`·`Limiter` 의 정적 곡선이다(입력 dB → 출력 dB, 어택·릴리스와 메이크업은 없다). 그 밖의 종류는 입력 그대로다.
    Float AudioEffectCompressedDb(const AudioEffectSettings& effect, Float inputDb);

    // `Reverb` 꼬리의 크기다(소리가 끊긴 뒤 `seconds` 초, 1 에서 시작). 콤 필터 여덟의 되먹임과 평균 줄 길이로 잰다 - 감쇠(`damping`)가
    // 높은 소리를 먼저 깎는 것은 담지 않는다. 그 밖의 종류는 0 이다.
    Float AudioReverbTail(const AudioEffectSettings& effect, Float seconds);
}
