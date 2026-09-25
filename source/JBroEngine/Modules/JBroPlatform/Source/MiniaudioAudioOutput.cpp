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
                if (m_contextReady)
                {
                    ma_context_uninit(&m_context);
                    m_contextReady = false;
                }
            }

            bool Open(const AudioOutputDesc& desc)
            {
                ma_device_config config = ma_device_config_init(ma_device_type_playback);
                // 이름을 받았으면 그 장치를 찾는다. 장치 목록은 컨텍스트가 들고 있으므로 장치가 사는 동안 컨텍스트도 산다.
                ma_device_id chosen = {};
                if (desc.deviceName != nullptr && desc.deviceName[0] != '\0')
                {
                    if (ma_context_init(nullptr, 0, nullptr, &m_context) != MA_SUCCESS)
                    {
                        return false;
                    }
                    m_contextReady = true;
                    ma_device_info* playback = nullptr;
                    ma_uint32 playbackCount = 0;
                    bool found = false;
                    if (ma_context_get_devices(&m_context, &playback, &playbackCount, nullptr, nullptr) == MA_SUCCESS)
                    {
                        for (ma_uint32 index = 0; index < playbackCount; ++index)
                        {
                            if (std::strcmp(playback[index].name, desc.deviceName) == 0)
                            {
                                chosen = playback[index].id;
                                found = true;
                                break;
                            }
                        }
                    }
                    if (false == found)
                    {
                        return false;
                    }
                    config.playback.pDeviceID = &chosen;
                }
                config.playback.format = ma_format_f32;
                config.playback.channels = desc.channels;
                config.sampleRate = desc.sampleRate;
                config.periodSizeInFrames = desc.periodFrames;
                config.dataCallback = &DataCallback;
                config.notificationCallback = &Notify;
                config.pUserData = this;
                // 장치 스레드가 우리 콜백 앞에서 버퍼를 0 으로 채우지 않게 한다 - 믹서가 전부 채운다.
                config.noPreSilencedOutputBuffer = MA_TRUE;
                if (ma_device_init(m_contextReady ? &m_context : nullptr, &config, &m_device) != MA_SUCCESS)
                {
                    return false;
                }
                m_initialized = true;
                // 웹은 첫 사용자 입력 전까지 막혀 있다. miniaudio 가 그 입력에서 풀고 알린다(`unlocked`).
                m_waitingForGesture.store(m_device.pContext != nullptr && m_device.pContext->backend == ma_backend_webaudio,
                    std::memory_order_relaxed);
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
                m_lost.store(false, std::memory_order_relaxed);
                m_running.store(true, std::memory_order_release);
                if (ma_device_start(&m_device) != MA_SUCCESS)
                {
                    m_running.store(false, std::memory_order_release);
                    m_callback.store(nullptr, std::memory_order_release);
                    return false;
                }
                return true;
            }

            void Stop() override
            {
                // 먼저 "우리가 멈춘다" 를 적는다 - 그 뒤의 멈춤 알림을 잃음으로 세지 않는다.
                if (m_running.exchange(false, std::memory_order_acq_rel))
                {
                    // 장치 스레드가 콜백을 마칠 때까지 기다린 뒤 돌아온다(miniaudio `ma_device_stop`).
                    ma_device_stop(&m_device);
                }
                m_callback.store(nullptr, std::memory_order_release);
            }

            bool IsRunning() const override
            {
                return m_running.load(std::memory_order_acquire);
            }

            bool IsLost() const override
            {
                return m_lost.load(std::memory_order_acquire);
            }

            bool IsWaitingForUserGesture() const override
            {
                return m_waitingForGesture.load(std::memory_order_relaxed);
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
                // 기본 장치가 바뀌어 따라갔으면(알림은 다른 스레드에서 온다) 여기서 이름을 다시 읽는다.
                if (m_initialized && m_nameStale.exchange(false, std::memory_order_acq_rel))
                {
                    ma_device_get_name(const_cast<ma_device*>(&m_device), ma_device_type_playback, m_name, sizeof(m_name), nullptr);
                }
                return m_name;
            }

        private:
            // 장치 스레드(또는 백엔드의 알림 스레드)에서 온다. 원자 값만 적는다.
            static void Notify(const ma_device_notification* notification)
            {
                MiniaudioAudioOutput* self = static_cast<MiniaudioAudioOutput*>(notification->pDevice->pUserData);
                switch (notification->type)
                {
                case ma_device_notification_type_stopped:
                    // 우리가 멈춘 것이 아닌데 멈췄다 - 장치가 사라졌다.
                    if (self->m_running.load(std::memory_order_acquire))
                    {
                        self->m_lost.store(true, std::memory_order_release);
                    }
                    break;
                case ma_device_notification_type_rerouted:
                    self->m_nameStale.store(true, std::memory_order_release);
                    break;
                case ma_device_notification_type_unlocked:
                    self->m_waitingForGesture.store(false, std::memory_order_relaxed);
                    break;
                default:
                    break;
                }
            }

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

            ma_context m_context = {};
            bool m_contextReady = false;
            ma_device m_device = {};
            bool m_initialized = false;
            std::atomic<bool> m_running{false};
            std::atomic<bool> m_lost{false};
            mutable std::atomic<bool> m_nameStale{false};
            std::atomic<bool> m_waitingForGesture{false};
            std::atomic<AudioRenderCallback> m_callback{nullptr};
            std::atomic<void*> m_user{nullptr};
            mutable char m_name[256] = {};
        };
    }

    OwnerPtr<IAudioOutput> CreateMiniaudioOutput(const AudioOutputDesc& desc)
    {
        OwnerPtr<MiniaudioAudioOutput> output = MakeOwnerPtr<MiniaudioAudioOutput>();
        if (false == output->Open(desc))
        {
            if (desc.deviceName != nullptr && desc.deviceName[0] != '\0')
            {
                Log::Write(LogLevel::Warning, "audio", "the audio device '%s' is not available", desc.deviceName);
            }
            else
            {
                Log::Write(LogLevel::Warning, "audio", "no audio output device - the game runs silent");
            }
            return nullptr;
        }
        Log::Write(LogLevel::Info, "audio", "audio output: %s (%u Hz, %u channels)", output->GetDeviceName(),
            output->GetSampleRate(), output->GetChannels());
        return OwnerPtr<IAudioOutput>(std::move(output));
    }

    std::uint32_t EnumerateMiniaudioOutputs(AudioDeviceInfo* devices, std::uint32_t capacity)
    {
        ma_context context;
        if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS)
        {
            return 0;
        }
        ma_device_info* playback = nullptr;
        ma_uint32 playbackCount = 0;
        std::uint32_t count = 0;
        if (ma_context_get_devices(&context, &playback, &playbackCount, nullptr, nullptr) == MA_SUCCESS)
        {
            count = playbackCount;
            for (ma_uint32 index = 0; index < playbackCount && devices != nullptr && index < capacity; ++index)
            {
                AudioDeviceInfo& info = devices[index];
                const std::size_t length = std::strlen(playback[index].name);
                const std::size_t kept = length < sizeof(info.name) - 1 ? length : sizeof(info.name) - 1;
                std::memcpy(info.name, playback[index].name, kept);
                info.name[kept] = '\0';
                info.isDefault = playback[index].isDefault != MA_FALSE;
            }
        }
        ma_context_uninit(&context);
        return count;
    }
}
