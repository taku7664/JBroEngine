#include "MiniaudioAudioOutput.h"

#include <JBro/Core/Log.h>

#include <miniaudio.h>

#include <atomic>
#include <cstring>
#include <utility>

namespace JBro::Internal
{
    namespace
    {
        class MiniaudioAudioOutput final : public IAudioOutput
        {
        public:
            ~MiniaudioAudioOutput() override
            {
                Stop();
                if (m_initialized)
                {
                    ma_device_uninit(&m_device);
                    m_initialized = false;
                }
            }

            bool Open(const AudioOutputDesc& desc)
            {
                ma_device_config config = ma_device_config_init(ma_device_type_playback);
                config.playback.format = ma_format_f32;
                config.playback.channels = desc.channels;
                config.sampleRate = desc.sampleRate;
                config.periodSizeInFrames = desc.periodFrames;
                config.dataCallback = &DataCallback;
                config.pUserData = this;
                // 장치 스레드가 우리 콜백 앞에서 버퍼를 0 으로 채우지 않게 한다 - 믹서가 전부 채운다.
                config.noPreSilencedOutputBuffer = MA_TRUE;
                if (ma_device_init(nullptr, &config, &m_device) != MA_SUCCESS)
                {
                    return false;
                }
                m_initialized = true;
                if (ma_device_get_name(&m_device, ma_device_type_playback, m_name, sizeof(m_name), nullptr) != MA_SUCCESS)
                {
                    static const char fallback[] = "audio output";
                    std::memcpy(m_name, fallback, sizeof(fallback));
                }
                return true;
            }

            bool Start(AudioRenderCallback callback, void* user) override
            {
                if (false == m_initialized || callback == nullptr)
                {
                    return false;
                }
                Stop();
                m_user.store(user, std::memory_order_relaxed);
                m_callback.store(callback, std::memory_order_release);
                if (ma_device_start(&m_device) != MA_SUCCESS)
                {
                    m_callback.store(nullptr, std::memory_order_release);
                    return false;
                }
                m_running = true;
                return true;
            }

            void Stop() override
            {
                if (m_running)
                {
                    // 장치 스레드가 콜백을 마칠 때까지 기다린 뒤 돌아온다(miniaudio `ma_device_stop`).
                    ma_device_stop(&m_device);
                    m_running = false;
                }
                m_callback.store(nullptr, std::memory_order_release);
            }

            bool IsRunning() const override
            {
                return m_running;
            }

            std::uint32_t GetSampleRate() const override
            {
                return m_initialized ? m_device.sampleRate : 0;
            }

            std::uint32_t GetChannels() const override
            {
                return m_initialized ? m_device.playback.channels : 0;
            }

            const char* GetDeviceName() const override
            {
                return m_name;
            }

        private:
            static void DataCallback(ma_device* device, void* output, const void*, ma_uint32 frameCount)
            {
                MiniaudioAudioOutput* self = static_cast<MiniaudioAudioOutput*>(device->pUserData);
                const AudioRenderCallback callback = self->m_callback.load(std::memory_order_acquire);
                if (callback == nullptr)
                {
                    std::memset(output, 0, sizeof(float) * frameCount * device->playback.channels);
                    return;
                }
                callback(self->m_user.load(std::memory_order_relaxed), static_cast<float*>(output), frameCount);
            }

            ma_device m_device = {};
            bool m_initialized = false;
            bool m_running = false;
            std::atomic<AudioRenderCallback> m_callback{nullptr};
            std::atomic<void*> m_user{nullptr};
            char m_name[256] = {};
        };
    }

    OwnerPtr<IAudioOutput> CreateMiniaudioOutput(const AudioOutputDesc& desc)
    {
        OwnerPtr<MiniaudioAudioOutput> output = MakeOwnerPtr<MiniaudioAudioOutput>();
        if (false == output->Open(desc))
        {
            Log::Write(LogLevel::Warning, "audio", "no audio output device - the game runs silent");
            return nullptr;
        }
        Log::Write(LogLevel::Info, "audio", "audio output: %s (%u Hz, %u channels)", output->GetDeviceName(),
            output->GetSampleRate(), output->GetChannels());
        return OwnerPtr<IAudioOutput>(std::move(output));
    }
}
