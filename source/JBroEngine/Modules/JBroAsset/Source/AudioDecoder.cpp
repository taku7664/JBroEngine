#include <JBro/Asset/AudioDecoder.h>

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
}
