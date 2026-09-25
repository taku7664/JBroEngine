#include <JBro/Asset/AudioDecoder.h>

#include <JBro/Platform/Platform.h>

#include <miniaudio.h>

#include <cmath>
#include <utility>

namespace JBro
{
    namespace
    {
        // 임포트 경로라 할당은 miniaudio 기본(CRT)을 쓴다 - 프레임 규칙의 대상이 아니다.
        bool Open(JArrayView<std::byte> encoded, ma_decoder& decoder)
        {
            if (encoded.data == nullptr || encoded.size == 0)
            {
                return false;
            }
            const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
            return ma_decoder_init_memory(encoded.data, encoded.size, &config, &decoder) == MA_SUCCESS;
        }

        bool ReadFormat(ma_decoder& decoder, AudioFormat& format)
        {
            ma_format sampleFormat = ma_format_unknown;
            ma_uint32 channels = 0;
            ma_uint32 sampleRate = 0;
            if (ma_decoder_get_data_format(&decoder, &sampleFormat, &channels, &sampleRate, nullptr, 0) != MA_SUCCESS
                || channels == 0 || sampleRate == 0)
            {
                return false;
            }
            format.channels = channels;
            format.sampleRate = sampleRate;
            return true;
        }
    }

    bool ProbeAudio(JArrayView<std::byte> encoded, AudioFormat& format)
    {
        ma_decoder decoder;
        if (false == Open(encoded, decoder))
        {
            return false;
        }
        AudioFormat probed;
        bool ok = ReadFormat(decoder, probed);
        ma_uint64 frames = 0;
        if (ok && (ma_decoder_get_length_in_pcm_frames(&decoder, &frames) != MA_SUCCESS || frames == 0))
        {
            // 헤더가 길이를 말하지 않는다. 끝까지 풀어 센다.
            float scratch[4096];
            const ma_uint64 chunk = sizeof(scratch) / sizeof(float) / probed.channels;
            frames = 0;
            for (;;)
            {
                ma_uint64 read = 0;
                if (ma_decoder_read_pcm_frames(&decoder, scratch, chunk, &read) != MA_SUCCESS || read == 0)
                {
                    break;
                }
                frames += read;
            }
        }
        ma_decoder_uninit(&decoder);
        if (false == ok || frames == 0)
        {
            return false;
        }
        probed.frameCount = frames;
        format = probed;
        return true;
    }

    bool DecodeAudio(JArrayView<std::byte> encoded, AudioFormat& format, Array<float>& pcm)
    {
        ma_decoder decoder;
        if (false == Open(encoded, decoder))
        {
            return false;
        }
        AudioFormat decoded;
        if (false == ReadFormat(decoder, decoded))
        {
            ma_decoder_uninit(&decoder);
            return false;
        }
        Array<float> samples;
        ma_uint64 expected = 0;
        if (ma_decoder_get_length_in_pcm_frames(&decoder, &expected) == MA_SUCCESS && expected > 0)
        {
            samples.Reserve(static_cast<std::size_t>(expected) * decoded.channels);
        }
        const ma_uint64 chunk = 4096;
        ma_uint64 frames = 0;
        for (;;)
        {
            const std::size_t at = static_cast<std::size_t>(frames) * decoded.channels;
            samples.Resize(at + static_cast<std::size_t>(chunk) * decoded.channels);
            ma_uint64 read = 0;
            if (ma_decoder_read_pcm_frames(&decoder, samples.Data() + at, chunk, &read) != MA_SUCCESS || read == 0)
            {
                break;
            }
            frames += read;
            if (read < chunk)
            {
                break;
            }
        }
        ma_decoder_uninit(&decoder);
        samples.Resize(static_cast<std::size_t>(frames) * decoded.channels);
        if (frames == 0)
        {
            return false;
        }
        decoded.frameCount = frames;
        format = decoded;
        pcm = std::move(samples);
        return true;
    }

    void ComputeAudioPeaks(const float* pcm, std::uint64_t frameCount, std::uint32_t channels, std::uint32_t buckets,
        Array<float>& peaks)
    {
        peaks.Clear();
        if (pcm == nullptr || frameCount == 0 || channels == 0 || buckets == 0)
        {
            return;
        }
        peaks.Resize(buckets);
        for (std::uint32_t bucket = 0; bucket < buckets; ++bucket)
        {
            peaks[bucket] = 0.0f;
        }
        for (std::uint64_t frame = 0; frame < frameCount; ++frame)
        {
            const std::uint32_t bucket = static_cast<std::uint32_t>(frame * buckets / frameCount);
            for (std::uint32_t channel = 0; channel < channels; ++channel)
            {
                const float magnitude = std::fabs(pcm[frame * channels + channel]);
                if (magnitude > peaks[bucket])
                {
                    peaks[bucket] = magnitude > 1.0f ? 1.0f : magnitude;
                }
            }
        }
    }

    bool ComputeAudioPeaks(JArrayView<std::byte> encoded, std::uint32_t buckets, Array<float>& peaks)
    {
        AudioFormat format;
        if (buckets == 0 || false == ProbeAudio(encoded, format))
        {
            return false;
        }
        ma_decoder decoder;
        if (false == Open(encoded, decoder))
        {
            return false;
        }
        Array<float> result;
        result.Resize(buckets);
        for (std::uint32_t bucket = 0; bucket < buckets; ++bucket)
        {
            result[bucket] = 0.0f;
        }
        float scratch[4096];
        const ma_uint64 chunk = sizeof(scratch) / sizeof(float) / format.channels;
        std::uint64_t frame = 0;
        for (;;)
        {
            ma_uint64 read = 0;
            if (ma_decoder_read_pcm_frames(&decoder, scratch, chunk, &read) != MA_SUCCESS || read == 0)
            {
                break;
            }
            for (ma_uint64 index = 0; index < read && frame < format.frameCount; ++index, ++frame)
            {
                const std::uint32_t bucket = static_cast<std::uint32_t>(frame * buckets / format.frameCount);
                for (std::uint32_t channel = 0; channel < format.channels; ++channel)
                {
                    const float magnitude = std::fabs(scratch[index * format.channels + channel]);
                    if (magnitude > result[bucket])
                    {
                        result[bucket] = magnitude > 1.0f ? 1.0f : magnitude;
                    }
                }
            }
        }
        ma_decoder_uninit(&decoder);
        peaks = std::move(result);
        return true;
    }

    // miniaudio 의 가상 파일 시스템 하나로 연 파일을 넘긴다. `onOpen` 은 이미 연 파일을 내줄 뿐이다 - 경로를 다시 열지 않는다.
    struct AudioFileDecoder::State
    {
        ma_vfs_callbacks callbacks = {};
        OwnerPtr<IFileStream> file;
        ma_decoder decoder = {};
        bool open = false;
        AudioFormat format;

        static IFileStream* StreamOf(ma_vfs* vfs)
        {
            return reinterpret_cast<State*>(vfs)->file.Get();
        }

        static ma_result OnOpen(ma_vfs* vfs, const char*, ma_uint32, ma_vfs_file* file)
        {
            *file = StreamOf(vfs);
            return *file != nullptr ? MA_SUCCESS : MA_DOES_NOT_EXIST;
        }

        static ma_result OnOpenW(ma_vfs*, const wchar_t*, ma_uint32, ma_vfs_file*)
        {
            return MA_NOT_IMPLEMENTED;
        }

        static ma_result OnClose(ma_vfs*, ma_vfs_file)
        {
            return MA_SUCCESS;
        }

        static ma_result OnRead(ma_vfs*, ma_vfs_file file, void* destination, size_t bytes, size_t* read)
        {
            const std::size_t got = static_cast<IFileStream*>(file)->Read(destination, bytes);
            if (read != nullptr)
            {
                *read = got;
            }
            return got == 0 && bytes > 0 ? MA_AT_END : MA_SUCCESS;
        }

        static ma_result OnWrite(ma_vfs*, ma_vfs_file, const void*, size_t, size_t*)
        {
            return MA_NOT_IMPLEMENTED;
        }

        static ma_result OnSeek(ma_vfs*, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin)
        {
            const FileSeekOrigin from = origin == ma_seek_origin_start ? FileSeekOrigin::Begin
                : (origin == ma_seek_origin_current ? FileSeekOrigin::Current : FileSeekOrigin::End);
            return static_cast<IFileStream*>(file)->Seek(offset, from) ? MA_SUCCESS : MA_BAD_SEEK;
        }

        static ma_result OnTell(ma_vfs*, ma_vfs_file file, ma_int64* cursor)
        {
            const std::int64_t at = static_cast<IFileStream*>(file)->Tell();
            *cursor = at;
            return at >= 0 ? MA_SUCCESS : MA_ERROR;
        }

        static ma_result OnInfo(ma_vfs*, ma_vfs_file file, ma_file_info* info)
        {
            const std::int64_t size = static_cast<IFileStream*>(file)->GetSize();
            info->sizeInBytes = size > 0 ? static_cast<ma_uint64>(size) : 0;
            return size >= 0 ? MA_SUCCESS : MA_ERROR;
        }
    };

    AudioFileDecoder::AudioFileDecoder() = default;

    AudioFileDecoder::~AudioFileDecoder()
    {
        Close();
    }

    bool AudioFileDecoder::Open(OwnerPtr<IFileStream> file, const char* path)
    {
        Close();
        if (file.Get() == nullptr)
        {
            return false;
        }
        m_state = MakeOwnerPtr<State>();
        State& state = *m_state;
        state.callbacks.onOpen = &State::OnOpen;
        state.callbacks.onOpenW = &State::OnOpenW;
        state.callbacks.onClose = &State::OnClose;
        state.callbacks.onRead = &State::OnRead;
        state.callbacks.onWrite = &State::OnWrite;
        state.callbacks.onSeek = &State::OnSeek;
        state.callbacks.onTell = &State::OnTell;
        state.callbacks.onInfo = &State::OnInfo;
        state.file = std::move(file);
        const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
        if (ma_decoder_init_vfs(reinterpret_cast<ma_vfs*>(&state), path != nullptr ? path : "", &config, &state.decoder)
            != MA_SUCCESS)
        {
            m_state = nullptr;
            return false;
        }
        state.open = true;
        if (false == ReadFormat(state.decoder, state.format))
        {
            Close();
            return false;
        }
        ma_uint64 frames = 0;
        if (ma_decoder_get_length_in_pcm_frames(&state.decoder, &frames) == MA_SUCCESS)
        {
            state.format.frameCount = frames;
        }
        return true;
    }

    void AudioFileDecoder::Close()
    {
        if (m_state.Get() != nullptr && m_state->open)
        {
            ma_decoder_uninit(&m_state->decoder);
            m_state->open = false;
        }
        m_state = nullptr;
    }

    bool AudioFileDecoder::IsOpen() const
    {
        return m_state.Get() != nullptr && m_state->open;
    }

    AudioFormat AudioFileDecoder::GetFormat() const
    {
        return IsOpen() ? m_state->format : AudioFormat{};
    }

    std::uint64_t AudioFileDecoder::CountFrames()
    {
        if (false == IsOpen())
        {
            return 0;
        }
        if (m_state->format.frameCount != 0)
        {
            return m_state->format.frameCount;
        }
        float scratch[4096];
        const std::uint64_t chunk = sizeof(scratch) / sizeof(float) / m_state->format.channels;
        std::uint64_t frames = 0;
        for (;;)
        {
            const std::uint64_t read = Read(scratch, chunk);
            if (read == 0)
            {
                break;
            }
            frames += read;
        }
        m_state->format.frameCount = frames;
        Seek(0);
        return frames;
    }

    std::uint64_t AudioFileDecoder::Read(float* out, std::uint64_t frames)
    {
        if (false == IsOpen() || out == nullptr || frames == 0)
        {
            return 0;
        }
        ma_uint64 read = 0;
        const ma_result result = ma_decoder_read_pcm_frames(&m_state->decoder, out, frames, &read);
        if (result != MA_SUCCESS && result != MA_AT_END)
        {
            return 0;
        }
        return read;
    }

    bool AudioFileDecoder::Seek(std::uint64_t frame)
    {
        return IsOpen() && ma_decoder_seek_to_pcm_frame(&m_state->decoder, frame) == MA_SUCCESS;
    }

    bool ComputeAudioPeaks(AudioFileDecoder& decoder, std::uint32_t buckets, Array<float>& peaks)
    {
        const std::uint64_t frames = decoder.CountFrames();
        const AudioFormat format = decoder.GetFormat();
        if (buckets == 0 || frames == 0 || format.channels == 0)
        {
            return false;
        }
        Array<float> result;
        result.Resize(buckets);
        for (std::uint32_t bucket = 0; bucket < buckets; ++bucket)
        {
            result[bucket] = 0.0f;
        }
        float scratch[4096];
        const std::uint64_t chunk = sizeof(scratch) / sizeof(float) / format.channels;
        std::uint64_t frame = 0;
        for (;;)
        {
            const std::uint64_t read = decoder.Read(scratch, chunk);
            if (read == 0)
            {
                break;
            }
            for (std::uint64_t index = 0; index < read && frame < frames; ++index, ++frame)
            {
                const std::uint32_t bucket = static_cast<std::uint32_t>(frame * buckets / frames);
                for (std::uint32_t channel = 0; channel < format.channels; ++channel)
                {
                    const float magnitude = std::fabs(scratch[index * format.channels + channel]);
                    if (magnitude > result[bucket])
                    {
                        result[bucket] = magnitude > 1.0f ? 1.0f : magnitude;
                    }
                }
            }
        }
        peaks = std::move(result);
        return true;
    }
}
