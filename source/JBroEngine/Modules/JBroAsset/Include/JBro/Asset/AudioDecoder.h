#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Types/Array.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    // 오디오 파일 바이트의 형식이다. 채널과 샘플 레이트는 파일 그대로다 - 믹서가 장치 형식으로 바꾼다.
    struct AudioFormat
    {
        std::uint32_t sampleRate = 0;
        std::uint32_t channels = 0;
        std::uint64_t frameCount = 0;
    };

    // WAV·MP3·FLAC·OGG 를 읽는다(miniaudio `ma_decoder` + stb_vorbis, D-197). 파일은 열지 않는다 - 바이트는 플랫폼이
    // 읽어 온다(D-112). 실패하면 결과를 손대지 않고 false 다. 임포트 경로의 일이고 프레임 경로가 아니다.
    //
    // 형식과 길이만 읽는다(Streaming). 길이를 헤더가 말하지 않는 형식(MP3 일부)은 끝까지 풀어 센다.
    bool ProbeAudio(JArrayView<std::byte> encoded, AudioFormat& format);
    // 전부 f32 인터리브 PCM 으로 푼다(Decompressed).
    bool DecodeAudio(JArrayView<std::byte> encoded, AudioFormat& format, Array<float>& pcm);
    // 파형 그림용 봉우리다. `buckets` 칸마다 모든 채널의 최대 절댓값(0..1)을 준다. 에디터의 미리 듣기가 쓴다.
    bool ComputeAudioPeaks(JArrayView<std::byte> encoded, std::uint32_t buckets, Array<float>& peaks);
    void ComputeAudioPeaks(const float* pcm, std::uint64_t frameCount, std::uint32_t channels, std::uint32_t buckets,
        Array<float>& peaks);
}
