#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    class IFileStream;

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

    // 디스크에서 흘려 읽는 디코더다(D-203, `AudioImportMode::StreamFromDisk`). 연 파일(`IFileStream`)을 넘겨받아 조금씩 푼다 -
    // 파일 전체가 메모리에 오지 않는다. **한 번에 한 스레드만 쓴다**(오디오 스트리머의 스레드, 또는 임포트). 할당은
    // miniaudio 기본(CRT)이다 - 오디오 스레드가 아니므로 프레임 규칙의 대상이 아니다.
    class AudioFileDecoder
    {
    public:
        AudioFileDecoder();
        ~AudioFileDecoder();
        AudioFileDecoder(const AudioFileDecoder&) = delete;
        AudioFileDecoder& operator=(const AudioFileDecoder&) = delete;

        // `path` 는 확장자로 형식을 먼저 짐작하는 데만 쓴다. 실패하면 파일을 닫고 거짓이다.
        bool Open(OwnerPtr<IFileStream> file, const char* path);
        void Close();
        bool IsOpen() const;
        // 헤더가 길이를 말하지 않으면 `frameCount` 가 0 이다 - `CountFrames` 로 센다.
        AudioFormat GetFormat() const;
        // 끝까지 풀어 세고 처음으로 돌아간다. 임포트 때 한 번 쓴다.
        std::uint64_t CountFrames();
        // f32 인터리브로 최대 `frames` 프레임을 채우고 채운 수를 돌려준다. 끝이면 0 이다.
        std::uint64_t Read(float* out, std::uint64_t frames);
        bool Seek(std::uint64_t frame);

    private:
        struct State;
        OwnerPtr<State> m_state;
    };

    // 연 디코더를 끝까지 풀어 봉우리를 잰다(디스크 스트리밍 에셋의 파형).
    bool ComputeAudioPeaks(AudioFileDecoder& decoder, std::uint32_t buckets, Array<float>& peaks);
}
