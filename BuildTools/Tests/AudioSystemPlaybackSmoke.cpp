#include "Core/Asset/AssetManager.h"
#include "Core/Asset/AudioEffectAsset.h"
#include "Core/Build/BuildManifest.h"   // 버스 목록이 패키지까지 가는지 왕복 확인
#include "Core/Audio/IAudioBus.h"
#include "Core/Audio/IAudioDevice.h"
#include "Core/Audio/IAudioEffect.h"
#include "Core/Audio/IAudioListener.h"
#include "Core/Audio/IAudioPlayer.h"
#include "GameFramework/Audio/AudioSystem.h"
#include "GameFramework/Component/AudioComponents.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Canvas/CanvasRuntimeAccess.h"

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
        // 마지막 CreatePlayer 가 요청받은 버스 이름. AudioPlayer 의 Bus 프로퍼티가 실제로
        // 디스크립터까지 흘러가는지 보는 자리다 — 예전엔 여기가 늘 비어 있었다.
        std::string LastRequestedBus;
        bool        LastRequestHadBus = false;
    };

    // 디바이스가 돌려주는 버스 스텁. 이름만 있으면 라우팅 검사는 충분하다.
    class TestAudioBus final : public IAudioBus
    {
    public:
        explicit TestAudioBus(std::string name) : m_name(std::move(name)) {}
        const char* GetName() const override { return m_name.c_str(); }
        void  SetVolume(float volume) override { m_volume = volume; }
        float GetVolume() const override { return m_volume; }
        void  SetMuted(bool muted) override { m_muted = muted; }
        bool  IsMuted() const override { return m_muted; }
        void  AttachEffect(SafePtr<IAudioEffect>) override {}
        void  DetachAllEffects() override {}

    private:
        std::string m_name;
        float m_volume = 1.0f;
        bool  m_muted = false;
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
        OwnerPtr<IAudioPlayer> CreatePlayer(const AudioPlayerDesc& desc) override
        {
            ++m_state.CreateAttempts;
            m_state.LastRequestHadBus = desc.Bus.IsValid();
            m_state.LastRequestedBus = desc.Bus.IsValid() ? desc.Bus->GetName() : AUDIO_MASTER_BUS_NAME;
            if (m_state.FailCreate)
            {
                return nullptr;
            }
            const int id = ++m_state.NextPlayerId;
            return MakeOwnerPtr<TestAudioPlayer>(m_state, id);
        }
        OwnerPtr<IAudioBus> CreateBus(const char* name) override
        {
            return MakeOwnerPtr<TestAudioBus>(ResolveAudioBusName(name));
        }
        void ConfigureBuses(const std::vector<AudioBusDef>& buses) override
        {
            m_buses.clear();
            m_buses.push_back(MakeOwnerPtr<TestAudioBus>(AUDIO_MASTER_BUS_NAME));
            for (const AudioBusDef& bus : buses)
            {
                if (bus.Name.empty() || IsSameAudioBusName(bus.Name.c_str(), AUDIO_MASTER_BUS_NAME)) continue;
                OwnerPtr<TestAudioBus> created = MakeOwnerPtr<TestAudioBus>(bus.Name);
                created->SetVolume(bus.Volume);
                m_buses.push_back(std::move(created));
            }
        }
        std::vector<std::string> GetBusNames() const override
        {
            std::vector<std::string> names;
            for (const OwnerPtr<TestAudioBus>& bus : m_buses)
            {
                if (bus) names.emplace_back(bus->GetName());
            }
            return names;
        }
        // 실제 디바이스처럼 이름별로 같은 인스턴스를 돌려주고, 모르는 이름은 Master 로
        // 떨어뜨린다 — 매번 새로 만들면 호출부가 버스를 붙들고 있는 동안 수명이 끊긴다.
        SafePtr<IAudioBus> GetBus(const char* name) override
        {
            if (m_buses.empty())
            {
                ConfigureBuses({});
            }
            const char* wanted = ResolveAudioBusName(name);
            for (OwnerPtr<TestAudioBus>& bus : m_buses)
            {
                if (bus && IsSameAudioBusName(bus->GetName(), wanted))
                {
                    return bus.GetSafePtr();
                }
            }
            return m_buses.front().GetSafePtr();
        }
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
        std::vector<OwnerPtr<TestAudioBus>> m_buses;
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
    CGameCanvas canvas;
    CAudioSystem* system = CCanvasRuntimeAccess::AddSystem<CAudioSystem>(
        canvas, device.GetSafePtr(), assetManager.GetSafePtr());
    CGameObject* object = canvas.CreateGameObject("AudioOwner");
    AudioPlayer* player = object ? object->AddComponent<AudioPlayer>() : nullptr;
    if (nullptr == system || nullptr == player)
    {
        std::cerr << "Failed to create the audio test canvas.\n";
        return 4;
    }
    player->AudioGuid = audioGuid;

    system->Update(canvas);
    if (false == Expect(1 == state.CreateAttempts && 1 == state.PlayCalls,
        "PlayOnStart did not create and play exactly once on activation.")) return 5;

    state.EndedByPlayer[state.NextPlayerId] = true;
    system->Update(canvas);
    system->Update(canvas);
    if (false == Expect(1 == state.CreateAttempts,
        "A completed non-loop PlayOnStart player was recreated.")) return 6;

    player->SetEnabled(false);
    system->Update(canvas);
    state.EndedByPlayer.clear();
    player->SetEnabled(true);
    system->Update(canvas);
    if (false == Expect(2 == state.CreateAttempts && 2 == state.PlayCalls,
        "Disable/Enable did not rearm PlayOnStart exactly once.")) return 7;

    player->AudioGuid = secondAudioGuid;
    system->Update(canvas);
    if (false == Expect(3 == state.CreateAttempts && 3 == state.PlayCalls,
        "Changing AudioGuid did not replace and play the source exactly once.")) return 8;

    player->Loop = true;
    state.EndedByPlayer[state.NextPlayerId] = true;
    system->Update(canvas);
    system->Update(canvas);
    if (false == Expect(3 == state.CreateAttempts,
        "A looping player was recreated or removed after reporting ended.")) return 9;

    player->SetEnabled(false);
    system->Update(canvas);
    state.FailCreate = true;
    player->SetEnabled(true);
    system->Update(canvas);
    system->Update(canvas);
    if (false == Expect(4 == state.CreateAttempts,
        "A failed player load was retried every frame.")) return 10;
    player->SetEnabled(false);
    system->Update(canvas);
    player->SetEnabled(true);
    system->Update(canvas);
    if (false == Expect(5 == state.CreateAttempts,
        "Disable/Enable did not explicitly retry a failed player load.")) return 11;

    state.FailCreate = false;
    player->SetEnabled(false);
    system->Update(canvas);
    player->EffectGuids = { effectGuid, effectGuid };
    player->SetEnabled(true);
    system->Update(canvas);
    if (false == Expect(2 == state.EffectCreations,
        "The N-effect chain did not create independent effect owners.")) return 12;

    state.Events.clear();
    system->SimulationStop(canvas);
    const std::size_t detach = FindEvent(state.Events, "detach");
    const std::size_t stop = FindEvent(state.Events, "stop");
    const std::size_t playerDestroy = FindEvent(state.Events, "player_destroy");
    const std::size_t effectDestroy = FindEvent(state.Events, "effect_destroy");
    if (false == Expect(detach < stop && stop < playerDestroy && playerDestroy < effectDestroy,
        "Simulation stop did not detach, stop, destroy player, then destroy effects.")) return 13;

    const int playsBeforeRestart = state.PlayCalls;
    system->Update(canvas);
    if (false == Expect(playsBeforeRestart + 1 == state.PlayCalls,
        "Simulation restart did not rearm PlayOnStart.")) return 14;

    // Rebuild and tear down one-effect and zero-effect variants as separate
    // simulation epochs. This catches stale effect owners left by chain shrink.
    player->EffectGuids = { effectGuid };
    system->Update(canvas);
    system->SimulationStop(canvas);
    system->Update(canvas);
    player->EffectGuids.Clear();
    system->Update(canvas);
    system->SimulationStop(canvas);
    system->Update(canvas);

    AudioPlayer* secondPlayer = object->AddComponent<AudioPlayer>();
    secondPlayer->AudioGuid = audioGuid;
    system->Update(canvas);
    const int createsWithTwoComponents = state.CreateAttempts;
    if (false == Expect(2 == state.LivePlayers,
        "Two AudioPlayer components did not keep independent live players.")) return 15;
    player->SetEnabled(false);
    system->Update(canvas);
    if (false == Expect(createsWithTwoComponents == state.CreateAttempts && 1 == state.LivePlayers,
        "Disabling one AudioPlayer disturbed another component instance.")) return 16;

    CCanvasRuntimeAccess::DestroyComponent(canvas, secondPlayer);
    system->Update(canvas);
    if (false == Expect(0 == state.LivePlayers,
        "Deleting an AudioPlayer component did not release only its player.")) return 17;
    system->SimulationStop(canvas);

    state.Events.clear();
    {
        CGameCanvas clearCanvas;
        CAudioSystem* clearSystem = CCanvasRuntimeAccess::AddSystem<CAudioSystem>(
            clearCanvas, device.GetSafePtr(), assetManager.GetSafePtr());
        CGameObject* clearObject = clearCanvas.CreateGameObject("CanvasClearAudioOwner");
        AudioPlayer* clearPlayer = clearObject ? clearObject->AddComponent<AudioPlayer>() : nullptr;
        if (nullptr == clearSystem || nullptr == clearPlayer)
        {
            std::cerr << "Failed to create the canvas-clear audio case.\n";
            return 18;
        }
        clearPlayer->AudioGuid = audioGuid;
        clearPlayer->EffectGuids = { effectGuid };
        clearSystem->Update(clearCanvas);
        state.Events.clear();
    }
    const std::size_t clearDetach = FindEvent(state.Events, "detach");
    const std::size_t clearStop = FindEvent(state.Events, "stop");
    const std::size_t clearPlayerDestroy = FindEvent(state.Events, "player_destroy");
    const std::size_t clearEffectDestroy = FindEvent(state.Events, "effect_destroy");
    if (false == Expect(
        clearDetach < clearStop && clearStop < clearPlayerDestroy && clearPlayerDestroy < clearEffectDestroy,
        "Canvas clear did not preserve deterministic player/effect teardown order.")) return 19;

    // ── Bus routing ────────────────────────────────────────────────────────
    // AudioPlayer 의 Bus 가 실제로 디스크립터까지 흘러가는지, 그리고 버스를 바꾸면
    // 인스턴스가 다시 만들어지는지. 후자는 miniaudio 가 살아 있는 sound 의 출력
    // group 을 못 바꾸기 때문에 필요한 계약이다 — 재생성이 빠지면 버스를 바꿔도
    // 소리는 옛 버스에 남고 인스펙터만 새 값을 보여 준다.
    {
        // 실제 흐름에서 ProjectManager(에디터) / 매니페스트(패키지) 가 하는 주입.
        // 이게 없으면 디바이스는 Master 하나뿐이라 무엇을 지정해도 Master 로 떨어진다.
        device->ConfigureBuses({ { "Music", 0.5f }, { "SFX", 1.0f }, { "Ambience", 0.25f } });

        CGameCanvas busCanvas;
        CAudioSystem* busSystem = CCanvasRuntimeAccess::AddSystem<CAudioSystem>(
            busCanvas, device.GetSafePtr(), assetManager.GetSafePtr());
        CGameObject* busObject = busCanvas.CreateGameObject("BusRoutingOwner");
        AudioPlayer* busPlayer = busObject ? busObject->AddComponent<AudioPlayer>() : nullptr;
        if (nullptr == busSystem || nullptr == busPlayer)
        {
            std::cerr << "Failed to create the bus routing case.\n";
            return 20;
        }

        // 기본값은 SFX — 버스를 저작하지 않은 기존 씬이 이 값으로 읽힌다.
        if (false == Expect("SFX" == busPlayer->Bus,
            "A fresh AudioPlayer did not default to the SFX bus.")) return 21;

        busPlayer->AudioGuid = audioGuid;
        busPlayer->Bus = "Music";
        busSystem->Update(busCanvas);
        if (false == Expect(state.LastRequestHadBus && "Music" == state.LastRequestedBus,
            "The audio system did not route the player into its bus.")) return 22;

        const int createsBeforeBusChange = state.CreateAttempts;
        busSystem->Update(busCanvas);
        if (false == Expect(createsBeforeBusChange == state.CreateAttempts,
            "An unchanged bus recreated the player every frame.")) return 23;

        // 프로젝트가 정의한 임의 이름으로도 라우팅돼야 한다 — enum 이던 시절엔 불가능했다.
        busPlayer->Bus = "Ambience";
        busSystem->Update(busCanvas);
        if (false == Expect(createsBeforeBusChange + 1 == state.CreateAttempts,
            "Changing the bus did not rebuild the backend instance.")) return 24;
        if (false == Expect("Ambience" == state.LastRequestedBus,
            "The rebuilt instance did not follow the new bus.")) return 25;

        busSystem->SimulationStop(busCanvas);
    }

    // ── 매니페스트 왕복 ─────────────────────────────────────────────────────
    // 버스 목록은 프로젝트 세팅(에디터)에만 있으면 소용이 없다. 패키지 게임은 매니페스트로
    // 받으므로 그 경로가 끊기면 **패키지에서만** 모든 소리가 Master 로 몰린다. 바이너리
    // 매니페스트는 난독화돼 있어 파일을 들여다봐도 확인할 수 없으니 왕복으로 본다.
    {
        BuildManifest written;
        written.StartupCanvasGuid = "0123456789abcdef0123456789abcdef";
        written.ProductName = "BusRoundTrip";
        written.AudioBuses = { { "Music", 0.5f }, { "SFX", 1.0f }, { "Ambience", 0.25f } };

        const std::filesystem::path manifestPath = root / "roundtrip.jbmanifest";
        std::string manifestError;
        if (false == CBuildManifestLoader::WriteBinaryFile(
            File::Path(manifestPath.generic_string()), written, &manifestError))
        {
            std::cerr << "Failed to write the round-trip manifest: " << manifestError << '\n';
            return 26;
        }

        BuildManifest loaded;
        if (false == CBuildManifestLoader::LoadFromFile(
            File::Path(manifestPath.generic_string()), loaded, &manifestError))
        {
            std::cerr << "Failed to read the round-trip manifest: " << manifestError << '\n';
            return 27;
        }
        bool busesMatch = written.AudioBuses.size() == loaded.AudioBuses.size();
        for (std::size_t i = 0; busesMatch && i < written.AudioBuses.size(); ++i)
        {
            busesMatch = written.AudioBuses[i].Name == loaded.AudioBuses[i].Name
                && written.AudioBuses[i].Volume == loaded.AudioBuses[i].Volume;
        }
        // 음량까지 봐야 한다 — 이름만 비교하면 float 를 안 싣는 회귀를 놓친다.
        if (false == Expect(busesMatch,
            "Audio buses did not survive the build manifest round trip.")) return 28;
    }

    assetManager->Finalize();
    std::filesystem::remove_all(root, fileError);
    std::cout << "AudioSystem playback state smoke test passed.\n";
    return 0;
}
