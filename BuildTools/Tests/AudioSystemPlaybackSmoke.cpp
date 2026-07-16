#include "Core/Asset/AssetManager.h"
#include "Core/Asset/AudioEffectAsset.h"
#include "Core/Audio/IAudioBus.h"
#include "Core/Audio/IAudioDevice.h"
#include "Core/Audio/IAudioEffect.h"
#include "Core/Audio/IAudioListener.h"
#include "Core/Audio/IAudioPlayer.h"
#include "GameFramework/Audio/AudioSystem.h"
#include "GameFramework/Component/AudioComponents.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneRuntimeAccess.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    struct AudioTestState
    {
        int CreateAttempts = 0;
        int PlayCalls = 0;
        int StopCalls = 0;
        int DetachCalls = 0;
        int PlayerDestructions = 0;
        int EffectCreations = 0;
        int EffectDestructions = 0;
        int LivePlayers = 0;
        int NextPlayerId = 0;
        bool FailCreate = false;
        std::unordered_map<int, bool> EndedByPlayer;
        std::vector<std::string> Events;
    };

    class TestAudioPlayer final : public IAudioPlayer
    {
    public:
        TestAudioPlayer(AudioTestState& state, int id)
            : m_state(state), m_id(id)
        {
            m_state.EndedByPlayer[m_id] = false;
            ++m_state.LivePlayers;
        }

        ~TestAudioPlayer() override
        {
            ++m_state.PlayerDestructions;
            --m_state.LivePlayers;
            m_state.Events.push_back("player_destroy");
            m_state.EndedByPlayer.erase(m_id);
        }

        void Play() override
        {
            ++m_state.PlayCalls;
            m_state.Events.push_back("play");
        }
        void Pause() override {}
        void Stop() override
        {
            ++m_state.StopCalls;
            m_state.Events.push_back("stop");
        }
        bool IsPlaying() const override { return false; }
        bool IsEnded() const override
        {
            const auto it = m_state.EndedByPlayer.find(m_id);
            return it != m_state.EndedByPlayer.end() && it->second;
        }

        void PlayAt(double) override {}
        std::uint64_t GetPositionFrames() const override { return 0; }
        double GetPositionSeconds() const override { return 0.0; }
        double GetDurationSeconds() const override { return 1.0; }
        void Seek(std::uint64_t) override {}
        void SetVolume(float) override {}
        void SetPitch(float) override {}
        void SetLoop(bool loop) override { m_loop = loop; }
        void SetPosition(AudioVec3) override {}
        void SetSpatial(const AudioSpatialParams&) override {}
        void AttachEffect(SafePtr<IAudioEffect>) override {}
        void SetEffectChain(const std::vector<SafePtr<IAudioEffect>>&) override {}
        void DetachAllEffects() override
        {
            ++m_state.DetachCalls;
            m_state.Events.push_back("detach");
        }

        int GetId() const { return m_id; }

    private:
        AudioTestState& m_state;
        int m_id = 0;
        bool m_loop = false;
    };

    class TestAudioEffect final : public IAudioEffect
    {
    public:
        TestAudioEffect(AudioTestState& state, EAudioEffectKind kind)
            : m_state(state), m_kind(kind)
        {
            ++m_state.EffectCreations;
        }
        ~TestAudioEffect() override
        {
            ++m_state.EffectDestructions;
            m_state.Events.push_back("effect_destroy");
        }
        EAudioEffectKind GetKind() const override { return m_kind; }
        void SetParameter(const char*, float) override {}
        float GetParameter(const char*) const override { return 0.0f; }

    private:
        AudioTestState& m_state;
        EAudioEffectKind m_kind;
    };

    class TestAudioDevice final : public IAudioDevice
    {
    public:
        explicit TestAudioDevice(AudioTestState& state) : m_state(state) {}

        bool Initialize(const AudioDeviceDesc&) override { return true; }
        void Finalize() override {}
        void Tick(float) override {}
        OwnerPtr<IAudioPlayer> CreatePlayer(const AudioPlayerDesc&) override
        {
            ++m_state.CreateAttempts;
            if (m_state.FailCreate)
            {
                return nullptr;
            }
            const int id = ++m_state.NextPlayerId;
            return MakeOwnerPtr<TestAudioPlayer>(m_state, id);
        }
        OwnerPtr<IAudioBus> CreateBus(EAudioBusKind) override { return nullptr; }
        SafePtr<IAudioBus> GetBus(EAudioBusKind) override { return nullptr; }
        OwnerPtr<IAudioEffect> CreateEffect(EAudioEffectKind kind) override
        {
            return MakeOwnerPtr<TestAudioEffect>(m_state, kind);
        }
        SafePtr<IAudioListener> GetPrimaryListener() override { return nullptr; }
        double GetGlobalAudioTimeSeconds() const override { return 0.0; }
        double GetOutputLatencySeconds() const override { return 0.0; }
        void RegisterPlayerMarker(SafePtr<IAudioPlayer>, std::uint64_t, std::function<void()>) override {}
        void SetMasterVolume(float) override {}
        float GetMasterVolume() const override { return 1.0f; }

    private:
        AudioTestState& m_state;
    };

    bool Expect(bool condition, const char* message)
    {
        if (condition)
        {
            return true;
        }
        std::cerr << message << '\n';
        return false;
    }

    bool RegisterAsset(CAssetManager& manager, const File::Path& path, EAssetType type, AssetGuid& outGuid)
    {
        if (false == manager.RegisterAssetByPath(path, type, true))
        {
            return false;
        }
        AssetMetaData meta;
        if (false == manager.GetRegistry().TryGetAssetByPath(path, meta))
        {
            return false;
        }
        outGuid = meta.Guid;
        return true;
    }

    std::size_t FindEvent(const std::vector<std::string>& events, const char* event)
    {
        const auto it = std::find(events.begin(), events.end(), event);
        return it == events.end() ? events.size() : static_cast<std::size_t>(it - events.begin());
    }
}

int main()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / L"jbro_audio_system_playback_smoke";
    std::error_code fileError;
    std::filesystem::remove_all(root, fileError);
    std::filesystem::create_directories(root, fileError);
    if (fileError)
    {
        std::cerr << "Failed to create the audio system test directory.\n";
        return 1;
    }

    const std::filesystem::path audioPath = root / L"tone.wav";
    const std::filesystem::path secondAudioPath = root / L"tone2.wav";
    const std::filesystem::path effectPath = root / L"effect.jfx";
    std::ofstream(audioPath).put('\0');
    std::ofstream(secondAudioPath).put('\0');
    std::ofstream(effectPath) << "Effect:\n  Kind: Reverb\n  Parameters:\n    wet: 0.5\n";

    AudioTestState state;
    OwnerPtr<CAssetManager> assetManager = MakeOwnerPtr<CAssetManager>();
    AssetManagerDesc assetDesc;
    assetDesc.AssetRootPath = File::Path(root.generic_string());
    if (false == assetManager->Initialize(assetDesc)
        || false == assetManager->RegisterLoader(MakeOwnerPtr<CAudioEffectAssetLoader>()))
    {
        std::cerr << "Failed to initialize the test asset manager.\n";
        return 2;
    }

    AssetGuid audioGuid;
    AssetGuid secondAudioGuid;
    AssetGuid effectGuid;
    if (false == RegisterAsset(*assetManager, File::Path(audioPath.generic_string()), EAssetType::Audio, audioGuid)
        || false == RegisterAsset(*assetManager, File::Path(secondAudioPath.generic_string()), EAssetType::Audio, secondAudioGuid)
        || false == RegisterAsset(*assetManager, File::Path(effectPath.generic_string()), EAssetType::AudioEffect, effectGuid))
    {
        std::cerr << "Failed to register test assets.\n";
        return 3;
    }

    OwnerPtr<TestAudioDevice> device = MakeOwnerPtr<TestAudioDevice>(state);
    CGameScene scene;
    CAudioSystem* system = CSceneRuntimeAccess::AddSystem<CAudioSystem>(
        scene, device.GetSafePtr(), assetManager.GetSafePtr());
    CGameObject* object = scene.CreateGameObject("AudioOwner");
    AudioPlayer* player = object ? object->AddComponent<AudioPlayer>() : nullptr;
    if (nullptr == system || nullptr == player)
    {
        std::cerr << "Failed to create the audio test scene.\n";
        return 4;
    }
    player->AudioGuid = audioGuid;

    system->Update(scene);
    if (false == Expect(1 == state.CreateAttempts && 1 == state.PlayCalls,
        "PlayOnStart did not create and play exactly once on activation.")) return 5;

    state.EndedByPlayer[state.NextPlayerId] = true;
    system->Update(scene);
    system->Update(scene);
    if (false == Expect(1 == state.CreateAttempts,
        "A completed non-loop PlayOnStart player was recreated.")) return 6;

    player->SetEnabled(false);
    system->Update(scene);
    state.EndedByPlayer.clear();
    player->SetEnabled(true);
    system->Update(scene);
    if (false == Expect(2 == state.CreateAttempts && 2 == state.PlayCalls,
        "Disable/Enable did not rearm PlayOnStart exactly once.")) return 7;

    player->AudioGuid = secondAudioGuid;
    system->Update(scene);
    if (false == Expect(3 == state.CreateAttempts && 3 == state.PlayCalls,
        "Changing AudioGuid did not replace and play the source exactly once.")) return 8;

    player->Loop = true;
    state.EndedByPlayer[state.NextPlayerId] = true;
    system->Update(scene);
    system->Update(scene);
    if (false == Expect(3 == state.CreateAttempts,
        "A looping player was recreated or removed after reporting ended.")) return 9;

    player->SetEnabled(false);
    system->Update(scene);
    state.FailCreate = true;
    player->SetEnabled(true);
    system->Update(scene);
    system->Update(scene);
    if (false == Expect(4 == state.CreateAttempts,
        "A failed player load was retried every frame.")) return 10;
    player->SetEnabled(false);
    system->Update(scene);
    player->SetEnabled(true);
    system->Update(scene);
    if (false == Expect(5 == state.CreateAttempts,
        "Disable/Enable did not explicitly retry a failed player load.")) return 11;

    state.FailCreate = false;
    player->SetEnabled(false);
    system->Update(scene);
    player->EffectGuids = { effectGuid, effectGuid };
    player->SetEnabled(true);
    system->Update(scene);
    if (false == Expect(2 == state.EffectCreations,
        "The N-effect chain did not create independent effect owners.")) return 12;

    state.Events.clear();
    system->SimulationStop(scene);
    const std::size_t detach = FindEvent(state.Events, "detach");
    const std::size_t stop = FindEvent(state.Events, "stop");
    const std::size_t playerDestroy = FindEvent(state.Events, "player_destroy");
    const std::size_t effectDestroy = FindEvent(state.Events, "effect_destroy");
    if (false == Expect(detach < stop && stop < playerDestroy && playerDestroy < effectDestroy,
        "Simulation stop did not detach, stop, destroy player, then destroy effects.")) return 13;

    const int playsBeforeRestart = state.PlayCalls;
    system->Update(scene);
    if (false == Expect(playsBeforeRestart + 1 == state.PlayCalls,
        "Simulation restart did not rearm PlayOnStart.")) return 14;

    // Rebuild and tear down one-effect and zero-effect variants as separate
    // simulation epochs. This catches stale effect owners left by chain shrink.
    player->EffectGuids = { effectGuid };
    system->Update(scene);
    system->SimulationStop(scene);
    system->Update(scene);
    player->EffectGuids.clear();
    system->Update(scene);
    system->SimulationStop(scene);
    system->Update(scene);

    AudioPlayer* secondPlayer = object->AddComponent<AudioPlayer>();
    secondPlayer->AudioGuid = audioGuid;
    system->Update(scene);
    const int createsWithTwoComponents = state.CreateAttempts;
    if (false == Expect(2 == state.LivePlayers,
        "Two AudioPlayer components did not keep independent live players.")) return 15;
    player->SetEnabled(false);
    system->Update(scene);
    if (false == Expect(createsWithTwoComponents == state.CreateAttempts && 1 == state.LivePlayers,
        "Disabling one AudioPlayer disturbed another component instance.")) return 16;

    CSceneRuntimeAccess::DestroyComponent(scene, secondPlayer);
    system->Update(scene);
    if (false == Expect(0 == state.LivePlayers,
        "Deleting an AudioPlayer component did not release only its player.")) return 17;
    system->SimulationStop(scene);

    state.Events.clear();
    {
        CGameScene clearScene;
        CAudioSystem* clearSystem = CSceneRuntimeAccess::AddSystem<CAudioSystem>(
            clearScene, device.GetSafePtr(), assetManager.GetSafePtr());
        CGameObject* clearObject = clearScene.CreateGameObject("SceneClearAudioOwner");
        AudioPlayer* clearPlayer = clearObject ? clearObject->AddComponent<AudioPlayer>() : nullptr;
        if (nullptr == clearSystem || nullptr == clearPlayer)
        {
            std::cerr << "Failed to create the scene-clear audio case.\n";
            return 18;
        }
        clearPlayer->AudioGuid = audioGuid;
        clearPlayer->EffectGuids = { effectGuid };
        clearSystem->Update(clearScene);
        state.Events.clear();
    }
    const std::size_t clearDetach = FindEvent(state.Events, "detach");
    const std::size_t clearStop = FindEvent(state.Events, "stop");
    const std::size_t clearPlayerDestroy = FindEvent(state.Events, "player_destroy");
    const std::size_t clearEffectDestroy = FindEvent(state.Events, "effect_destroy");
    if (false == Expect(
        clearDetach < clearStop && clearStop < clearPlayerDestroy && clearPlayerDestroy < clearEffectDestroy,
        "Scene clear did not preserve deterministic player/effect teardown order.")) return 19;

    assetManager->Finalize();
    std::filesystem::remove_all(root, fileError);
    std::cout << "AudioSystem playback state smoke test passed.\n";
    return 0;
}
