#include "pch.h"
#include "EditorAudioPreview.h"

#include "Engine/Core/Audio/AudioTypes.h"
#include "Engine/Core/Audio/IAudioDevice.h"
#include "Engine/Core/Audio/IAudioPlayer.h"
#include "Engine/Core/Audio/MiniAudio/MiniAudioDevice.h"
#include "Engine/Core/Audio/EmptyAudio/EmptyAudioDevice.h"

namespace
{
    // 모듈-로컬 상태. EnsureInitialized 가 처음 호출될 때만 디바이스가 만들어진다.
    OwnerPtr<IAudioDevice>  g_device;
    OwnerPtr<IAudioPlayer>  g_player;
    AssetGuid               g_currentGuid = File::NULL_GUID;
    bool                    g_initialized   = false;
    bool                    g_initFailed    = false;

#if defined(JBRO_HAS_MINIAUDIO) && JBRO_HAS_MINIAUDIO
    CMiniAudioDevice* GetMiniAudioDevice()
    {
        return static_cast<CMiniAudioDevice*>(g_device.Get());
    }
#endif
}

namespace EditorAudioPreview
{

void EnsureInitialized()
{
    if (g_initialized || g_initFailed) return;

#if defined(JBRO_HAS_MINIAUDIO) && JBRO_HAS_MINIAUDIO
    OwnerPtr<CMiniAudioDevice> miniDevice = MakeOwnerPtr<CMiniAudioDevice>();
    AudioDeviceDesc desc;
    if (false == miniDevice->Initialize(desc))
    {
        g_initFailed = true;
        return;
    }
    g_device = std::move(miniDevice);
#else
    OwnerPtr<CEmptyAudioDevice> empty = MakeOwnerPtr<CEmptyAudioDevice>();
    AudioDeviceDesc desc;
    empty->Initialize(desc);
    g_device = std::move(empty);
#endif

    g_initialized = true;
}

void Shutdown()
{
    Stop();
    if (g_device)
    {
        g_device->Finalize();
        g_device.Reset();
    }
    g_initialized = false;
    g_initFailed  = false;
}

void PlayFile(const char* absPathUtf8, const AssetGuid& guid)
{
    EnsureInitialized();
    if (false == g_initialized || nullptr == absPathUtf8) return;

    // 이전 재생이 있다면 무조건 정리 — 자원 누수 방지.
    Stop();

#if defined(JBRO_HAS_MINIAUDIO) && JBRO_HAS_MINIAUDIO
    if (CMiniAudioDevice* mini = GetMiniAudioDevice())
    {
        g_player      = mini->CreatePlayerFromFile(absPathUtf8);
        g_currentGuid = guid;
        if (g_player)
        {
            g_player->Play();
        }
        return;
    }
#endif
    // 빈 백엔드 — 재생 동작 없음. 상태만 추적.
    g_currentGuid = guid;
}

void Stop()
{
    if (g_player)
    {
        g_player->Stop();
        g_player.Reset();    // ma_sound_uninit 가 소멸자에서 호출됨.
    }
    g_currentGuid = File::NULL_GUID;
}

const AssetGuid& GetCurrentGuid()
{
    return g_currentGuid;
}

bool IsPlaying()
{
    return g_player && g_player->IsPlaying();
}

double GetCurrentPositionSeconds()
{
    return g_player ? g_player->GetPositionSeconds() : 0.0;
}

double GetCurrentDurationSeconds()
{
    return g_player ? g_player->GetDurationSeconds() : 0.0;
}

void SeekSeconds(double seconds)
{
    if (g_player) g_player->SeekSeconds(seconds);
}

} // namespace EditorAudioPreview
