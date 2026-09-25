#include <JBro/Platform/WindowsPlatform.h>

#include "MiniaudioAudioOutput.h"

#include <windows.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <thread>

// 구현은 miniaudio 의 `ma_device`(WASAPI) 다(D-197). 플랫폼은 그것을 내어 줄 뿐이다 - 믹싱은 엔진의 `AudioMixer` 가 한다.
namespace JBro
{
    namespace
    {
        // 출력 장치가 바뀌면 Windows 가 부른다(아무 스레드에서나). 표지 하나만 세운다.
        class DeviceNotifier final : public IMMNotificationClient
        {
        public:
            explicit DeviceNotifier(std::atomic<bool>& changed) : m_changed(changed)
            {
            }

            ULONG STDMETHODCALLTYPE AddRef() override
            {
                return static_cast<ULONG>(m_references.fetch_add(1) + 1);
            }

            ULONG STDMETHODCALLTYPE Release() override
            {
                // 수명은 감시가 쥔다 - 0 이 되어도 여기서 지우지 않는다.
                return static_cast<ULONG>(m_references.fetch_sub(1) - 1);
            }

            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
            {
                if (id == __uuidof(IUnknown) || id == __uuidof(IMMNotificationClient))
                {
                    *out = static_cast<IMMNotificationClient*>(this);
                    AddRef();
                    return S_OK;
                }
                *out = nullptr;
                return E_NOINTERFACE;
            }

            HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override
            {
                return Mark();
            }

            HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override
            {
                return Mark();
            }

            HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override
            {
                return Mark();
            }

            HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole, LPCWSTR) override
            {
                return flow == eRender ? Mark() : S_OK;
            }

            HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override
            {
                return S_OK;
            }

        private:
            HRESULT Mark()
            {
                m_changed.store(true, std::memory_order_release);
                return S_OK;
            }

            std::atomic<bool>& m_changed;
            std::atomic<long> m_references{1};
        };
    }

    // 장치 알림을 제 스레드에서 등록한다(D-206). 메인 스레드의 COM 방식(파일 대화상자는 STA 로 켜고 끈다)과 섞이지 않게
    // 멀티스레드 아파트를 이 스레드에만 둔다. 알림은 Windows 의 스레드에서 오고 표지 하나만 세운다.
    class AudioDeviceWatch
    {
    public:
        AudioDeviceWatch() : m_notifier(changed)
        {
            m_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            m_thread = std::thread([this] { Run(); });
        }

        ~AudioDeviceWatch()
        {
            SetEvent(m_stop);
            m_thread.join();
            CloseHandle(m_stop);
        }

        std::atomic<bool> changed{false};

    private:
        void Run()
        {
            const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            IMMDeviceEnumerator* enumerator = nullptr;
            if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                    reinterpret_cast<void**>(&enumerator))))
            {
                const bool registered = SUCCEEDED(enumerator->RegisterEndpointNotificationCallback(&m_notifier));
                WaitForSingleObject(m_stop, INFINITE);
                if (registered)
                {
                    enumerator->UnregisterEndpointNotificationCallback(&m_notifier);
                }
                enumerator->Release();
            }
            else
            {
                WaitForSingleObject(m_stop, INFINITE);
            }
            if (SUCCEEDED(com))
            {
                CoUninitialize();
            }
        }

        DeviceNotifier m_notifier;
        HANDLE m_stop = nullptr;
        std::thread m_thread;
    };

    bool WindowsPlatform::TakeAudioDevicesChanged()
    {
        if (m_audioWatch.Get() == nullptr)
        {
            m_audioWatch = MakeOwnerPtr<AudioDeviceWatch>();
            return false;
        }
        return m_audioWatch->changed.exchange(false, std::memory_order_acq_rel);
    }

    OwnerPtr<IAudioOutput> WindowsPlatform::CreateAudioOutput(const AudioOutputDesc& desc)
    {
        return Internal::CreateMiniaudioOutput(desc);
    }

    std::uint32_t WindowsPlatform::EnumerateAudioOutputs(AudioDeviceInfo* devices, std::uint32_t capacity)
    {
        return Internal::EnumerateMiniaudioOutputs(devices, capacity);
    }
}
