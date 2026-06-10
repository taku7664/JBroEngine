#include "pch.h"
#include "EditorAudioPreview.h"

#include "Engine/Core/Audio/AudioTypes.h"
#include "Engine/Core/Audio/IAudioDevice.h"
#include "Engine/Core/Audio/IAudioPlayer.h"
#include "Engine/Core/Audio/IAudioEffect.h"
#include "Engine/Core/Audio/MiniAudio/MiniAudioDevice.h"
#include "Engine/Core/Audio/EmptyAudio/EmptyAudioDevice.h"

namespace
{
    // 모듈-로컬 상태. EnsureInitialized 가 처음 호출될 때만 디바이스가 만들어진다.
    OwnerPtr<IAudioDevice>  g_device;
    OwnerPtr<IAudioPlayer>  g_player;
    OwnerPtr<IAudioEffect>  g_effect;   // 미리듣기에 적용된 효과 (있을 때만)
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
        g_player->DetachAllEffects();
        g_player->Stop();
        g_player.Reset();    // ma_sound_uninit 가 소멸자에서 호출됨.
    }
    g_effect.Reset();        // player 해제 후 효과 노드 해제.
    g_currentGuid = File::NULL_GUID;
}

void PlayFileWithEffect(const char* absPathUtf8, const AssetGuid& guid,
                        EAudioEffectKind kind, const std::map<std::string, float>& params)
{
    EnsureInitialized();
    if (false == g_initialized || nullptr == absPathUtf8) return;

    Stop();

#if defined(JBRO_HAS_MINIAUDIO) && JBRO_HAS_MINIAUDIO
    if (CMiniAudioDevice* mini = GetMiniAudioDevice())
    {
        g_player      = mini->CreatePlayerFromFile(absPathUtf8);
        g_currentGuid = guid;

        // 효과 노드 생성 + 파라미터 적용 + 부착.
        g_effect = mini->CreateEffect(kind);
        if (g_effect)
        {
            for (const auto& kv : params)
            {
                g_effect->SetParameter(kv.first.c_str(), kv.second);
            }
            if (g_player) g_player->AttachEffect(g_effect.GetSafePtr());
        }
        if (g_player) g_player->Play();
        return;
    }
#endif
    g_currentGuid = guid;
}

void UpdatePreviewEffect(EAudioEffectKind kind, const std::map<std::string, float>& params)
{
    // 재생 중이 아니면 적용할 대상이 없다 — no-op (다음 Play 때 반영됨).
    if (!g_player || false == g_player->IsPlaying()) return;

#if defined(JBRO_HAS_MINIAUDIO) && JBRO_HAS_MINIAUDIO
    CMiniAudioDevice* mini = GetMiniAudioDevice();
    if (nullptr == mini) return;

    // kind 가 같으면 기존 노드 파라미터만 갱신 — 재생이 끊기지 않는다.
    if (g_effect && g_effect->GetKind() == kind)
    {
        for (const auto& kv : params)
        {
            g_effect->SetParameter(kv.first.c_str(), kv.second);
        }
        return;
    }

    // kind 가 바뀌었으면 효과 노드를 새로 만들어 교체-부착한다.
    // player 는 그대로 유지하므로 재생 위치/상태가 보존된다.
    OwnerPtr<IAudioEffect> next = mini->CreateEffect(kind);
    if (!next)
    {
        // 새 효과 생성 실패 — 기존 효과를 떼고 원음으로 재생 지속.
        g_player->DetachAllEffects();
        g_effect.Reset();
        return;
    }
    for (const auto& kv : params)
    {
        next->SetParameter(kv.first.c_str(), kv.second);
    }
    g_player->AttachEffect(next.GetSafePtr());   // 내부에서 기존 효과를 떼고 새 노드를 배선.
    g_effect = std::move(next);
#else
    (void)kind;
    (void)params;
#endif
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
