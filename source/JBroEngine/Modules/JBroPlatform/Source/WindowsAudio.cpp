#include <JBro/Platform/WindowsPlatform.h>

#include "MiniaudioAudioOutput.h"

// 구현은 miniaudio 의 `ma_device`(WASAPI) 다(D-197). 플랫폼은 그것을 내어 줄 뿐이다 - 믹싱은 엔진의 `AudioMixer` 가 한다.
namespace JBro
{
    OwnerPtr<IAudioOutput> WindowsPlatform::CreateAudioOutput(const AudioOutputDesc& desc)
    {
        return Internal::CreateMiniaudioOutput(desc);
    }

    std::uint32_t WindowsPlatform::EnumerateAudioOutputs(AudioDeviceInfo* devices, std::uint32_t capacity)
    {
        return Internal::EnumerateMiniaudioOutputs(devices, capacity);
    }
}
