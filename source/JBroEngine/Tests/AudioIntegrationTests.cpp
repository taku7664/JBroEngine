#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/AudioDecoder.h>
#include <JBro/Audio/AudioMixer.h>
#include <JBro/Audio/AudioSystem.h>
#include <JBro/AudioTypes/Component/AudioSource.h>
#include <JBro/AudioTypes/Internal/SystemContext.h>
#include <JBro/AudioTypes/ServiceContext.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Core/Log.h>
#include <JBro/Framework2D/Component/AudioListener2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Host/IFramework.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Types/NameTable.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

// 오디오 2·3·4 단계(audio-plan §3): 프로젝트 파일의 버스, 에셋 로드와 해제 알림, 2D 소스 시스템, 스크립트 서비스.
// 장치는 열지 않는다 - 믹서를 직접 당겨 잰다.
namespace
{
    namespace fs = std::filesystem;
    using namespace JBro;

    constexpr std::uint32_t Rate = 48000;
    constexpr float Pi = 3.14159265358979f;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    String Utf8(const fs::path& path)
    {
        const std::u8string text = path.generic_u8string();
        return String(reinterpret_cast<const char*>(text.data()), text.size());
    }

    void WriteBytes(const fs::path& path, const void* data, std::size_t size)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    // 16 비트 스테레오 WAV. 진폭 0.5, 440 Hz.
    Array<std::uint8_t> MakeWav(std::uint32_t frames, float amplitude = 0.5f)
    {
        Array<std::uint8_t> bytes;
        const std::uint32_t dataBytes = frames * 4;
        bytes.Resize(44 + dataBytes);
        auto put32 = [&](std::size_t at, std::uint32_t value) { std::memcpy(bytes.Data() + at, &value, 4); };
        auto put16 = [&](std::size_t at, std::uint16_t value) { std::memcpy(bytes.Data() + at, &value, 2); };
        std::memcpy(bytes.Data(), "RIFF", 4);
        put32(4, 36 + dataBytes);
        std::memcpy(bytes.Data() + 8, "WAVEfmt ", 8);
        put32(16, 16);
        put16(20, 1);
        put16(22, 2);
        put32(24, Rate);
        put32(28, Rate * 4);
        put16(32, 4);
        put16(34, 16);
        std::memcpy(bytes.Data() + 36, "data", 4);
        put32(40, dataBytes);
        for (std::uint32_t frame = 0; frame < frames; ++frame)
        {
            const float value = amplitude * std::sin(2.0f * Pi * 440.0f * static_cast<float>(frame) / Rate);
            const std::int16_t sample = static_cast<std::int16_t>(value * 32767.0f);
            std::memcpy(bytes.Data() + 44 + frame * 4, &sample, 2);
            std::memcpy(bytes.Data() + 44 + frame * 4 + 2, &sample, 2);
        }
        return bytes;
    }

    struct Peaks
    {
        float left = 0.0f;
        float right = 0.0f;
    };

    Peaks RenderPeaks(AudioMixer& mixer, std::uint32_t frames)
    {
        Array<float> buffer;
        buffer.Resize(static_cast<std::size_t>(frames) * 2);
        std::uint32_t done = 0;
        while (done < frames)
        {
            const std::uint32_t chunk = frames - done < 480 ? frames - done : 480;
            mixer.Render(buffer.Data() + static_cast<std::size_t>(done) * 2, chunk);
            done += chunk;
        }
        Peaks peaks;
        // 페이드·시작 경계를 피하려고 뒤쪽 절반만 본다.
        for (std::uint32_t frame = frames / 2; frame < frames; ++frame)
        {
            peaks.left = std::fmax(peaks.left, std::fabs(buffer[frame * 2]));
            peaks.right = std::fmax(peaks.right, std::fabs(buffer[frame * 2 + 1]));
        }
        return peaks;
    }

    // ── 프로젝트 파일 ────────────────────────────────────────────────────────────────────────────
    constexpr char LegacyProject[] =
        "Version: 1\n"
        "EngineVersion: 0.1.0\n"
        "Framework: 2D\n"
        "AudioBuses:\n"
        "  - Name: Music\n"
        "    Volume: 0.8\n"
        "  - Name: SFX\n"
        "    Volume: 1\n"
        "  - Name: Ambience\n"
        "    Volume: 0.5\n"
        "InputActions:\n"
        "  - Name: MoveLeft\n"
        "    Key: A\n";

    void TestProjectFileReadsAndKeepsAudioBuses()
    {
        ProjectFile project;
        ProjectFileError error;
        Check(ParseProjectFile(LegacyProject, sizeof(LegacyProject) - 1, project, error),
            "a project with the old engine's AudioBuses reads");
        Check(project.audioBuses.Size() == 3, "three buses are read");
        Check(project.audioBuses[0].name == "Music" && std::fabs(project.audioBuses[0].volume - 0.8f) < 1e-6f,
            "the first bus keeps its name and volume");
        Check(project.audioBuses[2].name == "Ambience" && std::fabs(project.audioBuses[2].volume - 0.5f) < 1e-6f,
            "the last bus keeps its name and volume");

        // 바뀐 것이 없으면 버스 블록은 바이트 하나도 건드리지 않는다(D-189). `0.8` 이 `0.800000012` 가 되면 여기서 걸린다.
        // 이 표본에 없는 아는 키는 쓰는 쪽이 뒤에 붙인다(기존 규약) - 그래서 블록을 글자째로 찾는다.
        String written;
        Check(WriteProjectFileText(project, LegacyProject, sizeof(LegacyProject) - 1, written, error), "it writes back");
        Check(written.find("AudioBuses:\n  - Name: Music\n    Volume: 0.8\n  - Name: SFX\n    Volume: 1\n"
                           "  - Name: Ambience\n    Volume: 0.5\nInputActions:\n") != String::npos,
            "an unchanged save leaves the bus block byte for byte");
        String twice;
        Check(WriteProjectFileText(project, written.c_str(), written.size(), twice, error) && twice == written,
            "saving again changes nothing");

        project.audioBuses[1].volume = 0.25f;
        project.audioBuses.Add(ProjectAudioBus{String("UI: Menus"), 1.0f});
        Check(WriteProjectFileText(project, LegacyProject, sizeof(LegacyProject) - 1, written, error), "an edit writes");
        ProjectFile reread;
        Check(ParseProjectFile(written.c_str(), written.size(), reread, error), "the edited file reads back");
        Check(reread.audioBuses.Size() == 4 && std::fabs(reread.audioBuses[1].volume - 0.25f) < 1e-6f,
            "the changed volume is kept");
        Check(reread.audioBuses[3].name == "UI: Menus", "a name with a colon is quoted and survives");
        Check(written.find("InputActions:\n  - Name: MoveLeft") != String::npos, "the unknown block is left alone");

        project.audioBuses.Clear();
        Check(WriteProjectFileText(project, LegacyProject, sizeof(LegacyProject) - 1, written, error), "an empty list writes");
        Check(written.find("AudioBuses: []\n") != String::npos, "an empty list is written as []");
        Check(ParseProjectFile(written.c_str(), written.size(), reread, error) && reread.audioBuses.IsEmpty(),
            "and reads back as empty");

        // 이펙트 칸은 기본값과 다른 것만 적히고 되읽힌다(D-202).
        project.audioBuses.Add(ProjectAudioBus{String("Cave"), 1.0f});
        project.audioBuses.Last().effects.reverbMix = 0.4f;
        project.audioBuses.Last().effects.lowPassHz = 800.0f;
        Check(WriteProjectFileText(project, LegacyProject, sizeof(LegacyProject) - 1, written, error), "effects write");
        Check(written.find("  - Name: Cave\n    Volume: 1\n    LowPass: 800\n    ReverbMix: 0.4\n") != String::npos,
            "only the effect fields that differ from the defaults are written");
        Check(ParseProjectFile(written.c_str(), written.size(), reread, error) && reread.audioBuses.Last().effects.reverbMix == 0.4f
                && reread.audioBuses.Last().effects.lowPassHz == 800.0f && reread.audioBuses.Last().effects.echoMix == 0.0f,
            "and they read back");

        constexpr char bad[] = "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\nAudioBuses:\n  - Name: Music\n    Volume: loud\n";
        Check(false == ParseProjectFile(bad, sizeof(bad) - 1, reread, error) && error.line == 6,
            "a volume that is not a number is refused with its line");
    }

    // ── 에셋 ────────────────────────────────────────────────────────────────────────────────────
    struct Fixture
    {
        fs::path root;
        WindowsPlatform platform;
        JMemoryContext memory;
        AssetRegistry registry;
        AssetSystem assets;
        AssetId shortId;
        AssetId longId;

        void Open()
        {
            root = fs::temp_directory_path() / L"JBroAudioProbe·오디오";
            fs::remove_all(root);
            const Array<std::uint8_t> shortWav = MakeWav(Rate / 10);
            const Array<std::uint8_t> longWav = MakeWav(Rate);
            WriteBytes(root / "Sound" / "blip.wav", shortWav.Data(), shortWav.Size());
            WriteBytes(root / "Sound" / "theme.wav", longWav.Data(), longWav.Size());
            Check(platform.Initialize(memory), "the platform must initialize");
            AssetScanOptions options;
            options.createMissingMeta = true;
            AssetScanReport report;
            Check(registry.Scan(platform, Utf8(root).c_str(), options, report), "the folder scans");
            const AssetRecord* blip = registry.FindByPath("Sound/blip.wav");
            const AssetRecord* theme = registry.FindByPath("Sound/theme.wav");
            Check(blip != nullptr && theme != nullptr && blip->type == AssetType::Audio, "wav files register as audio");
            shortId = blip->id;
            longId = theme->id;
            // 긴 것은 스트리밍으로 둔다 - 메타의 `Audio.ImportOptions.mode` 다.
            const String metaPath = Utf8(root / "Sound" / "theme.wav.jmeta");
            AssetMetaFile meta;
            AssetMetaError metaError;
            Check(LoadAssetMetaFile(platform, metaPath.c_str(), meta, metaError), "the created meta reads");
            meta.hasAudioOptions = true;
            meta.audioOptions.mode = AudioImportMode::Streaming;
            Check(SaveAssetMetaFile(platform, metaPath.c_str(), meta), "the meta takes the streaming mode");
            AssetMetaFile reread;
            Check(LoadAssetMetaFile(platform, metaPath.c_str(), reread, metaError) && reread.hasAudioOptions
                && reread.audioOptions.mode == AudioImportMode::Streaming, "the audio block round-trips");
            Check(assets.Initialize(memory), "the asset system initializes");
            assets.Bind(platform, registry, Utf8(root).c_str());
        }

        void Close()
        {
            assets.Shutdown();
            platform.Shutdown();
            fs::remove_all(root);
        }
    };

    struct ReleaseProbe
    {
        std::uint32_t calls = 0;
        AssetHandle last;
        static void OnRelease(void* user, AssetHandle handle)
        {
            ReleaseProbe* probe = static_cast<ReleaseProbe*>(user);
            ++probe->calls;
            probe->last = handle;
        }
    };

    void TestAudioAssetsLoadAndAnnounceTheirRelease()
    {
        Fixture fixture;
        fixture.Open();
        AssetSystem& assets = fixture.assets;
        ReleaseProbe probe;
        assets.SetAudioReleaseListener(&ReleaseProbe::OnRelease, &probe);

        const AssetHandle blip = assets.Load(fixture.shortId);
        const AudioData* decoded = assets.GetAudio(blip);
        Check(decoded != nullptr, "a decompressed wav loads");
        Check(decoded->sampleRate == Rate && decoded->channels == 2 && decoded->frameCount == Rate / 10,
            "with the file's own format and length");
        Check(decoded->pcm.Size() == static_cast<std::size_t>(Rate / 10) * 2 && decoded->encoded.IsEmpty(),
            "as interleaved PCM and no encoded bytes");

        const AssetHandle theme = assets.Load(fixture.longId);
        const AudioData* streamed = assets.GetAudio(theme);
        Check(streamed != nullptr && streamed->options.mode == AudioImportMode::Streaming, "a streaming wav loads");
        Check(streamed->pcm.IsEmpty() && streamed->encoded.Size() == 44 + Rate * 4, "it keeps the file bytes, not PCM");
        Check(streamed->frameCount == Rate, "its length is probed");
        Check(assets.GetSprite(blip) == nullptr && assets.GetTexture(blip) == nullptr, "an audio handle is only audio");

        Array<float> peaks;
        JArrayView<std::byte> bytes;
        bytes.data = streamed->encoded.Data();
        bytes.size = static_cast<std::uint32_t>(streamed->encoded.Size());
        Check(ComputeAudioPeaks(bytes, 64, peaks) && peaks.Size() == 64, "the waveform peaks are computed from the bytes");
        Check(peaks[10] > 0.45f && peaks[10] < 0.55f, "a peak matches the tone's amplitude");

        // in-place 재로드는 자료를 바꾸기 전에 알린다. 핸들은 그대로다.
        Check(assets.ReloadInPlace(fixture.shortId), "a loaded clip reloads in place");
        Check(probe.calls == 1 && probe.last.index == blip.index && probe.last.generation == blip.generation,
            "the listener hears about the old data before it goes");
        Check(assets.GetAudio(blip) != nullptr && assets.GetAudio(blip)->dataGeneration == 2, "the handle lives on");

        assets.Release(blip);
        Check(assets.CollectUnused() == 1, "an unused clip is collected");
        Check(probe.calls == 2, "and the listener heard it first");
        assets.SetAudioReleaseListener(nullptr, nullptr);
        fixture.Close();
    }

    // ── 실제 장치 ──
    // 장치가 믹서를 당겨 가는지 **소리 없이** 본다(보이스가 없어 0 만 나간다). `Stop` 이 돌아온 뒤에는 더 당기지 않아야 한다 -
    // 그래야 믹서를 내려도 된다(D-201 의 종료 순서). 장치가 없는 기계(원격 세션)에서는 건너뛴다.
    void TestTheDevicePullsTheMixerAndStopsWhenAsked()
    {
        WindowsPlatform platform;
        JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");
        OwnerPtr<IAudioOutput> output = platform.CreateAudioOutput(AudioOutputDesc{});
        if (output.Get() == nullptr)
        {
            std::cout << "  [skip] no audio device on this machine" << std::endl;
            platform.Shutdown();
            return;
        }
        AudioMixerDesc desc;
        desc.sampleRate = output->GetSampleRate();
        desc.channels = output->GetChannels();
        desc.maxVoices = 4;
        AudioMixer mixer;
        Check(mixer.Initialize(desc), "a mixer in the device's format initializes");
        Check(output->Start(&AudioMixer::RenderCallback, &mixer), "the device starts");
        const auto start = std::chrono::steady_clock::now();
        while (mixer.GetStats().renderedFrames == 0
            && std::chrono::steady_clock::now() - start < std::chrono::seconds(2))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const std::uint64_t pulled = mixer.GetStats().renderedFrames;
        std::cout << "  the device " << output->GetDeviceName() << " pulled " << pulled << " frames in about 0.1 s" << std::endl;
        Check(pulled > 0, "the device pulls frames from the mixer");
        output->Stop();
        const std::uint64_t afterStop = mixer.GetStats().renderedFrames;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        Check(mixer.GetStats().renderedFrames == afterStop, "after Stop returns the device no longer calls the mixer");
        output = nullptr;
        mixer.Shutdown();
        platform.Shutdown();
    }

    // ── 2D 소스 시스템 ──────────────────────────────────────────────────────────────────────────
    struct Scene
    {
        Fixture fixture;
        AudioMixer mixer;
        System::AudioSystem audio;
        Framework2D framework;
        Canvas* canvas = nullptr;

        void Open()
        {
            fixture.Open();
            AudioMixerDesc desc;
            desc.maxVoices = 16;
            Check(mixer.Initialize(desc), "the mixer initializes");
            Check(audio.Initialize(mixer, &fixture.assets), "the audio system initializes");
            const AudioBusConfig buses[] = {{NameTable::Get().Intern("Music"), 0.5f}, {NameTable::Get().Intern("SFX"), 1.0f}};
            audio.ConfigureBuses({buses, 2});
            BindAudioSystemContext(audio.GetSystemContext());
            BindAudioServiceContext(audio.GetServiceContext());
            FrameworkContext context;
            context.memory = fixture.memory;
            context.assets = &fixture.assets;
            context.audio = &audio;
            Check(framework.Initialize(context), "a framework with audio initializes");
            canvas = framework.GetCanvas();
        }

        Component::AudioSource* AddSource(const char* name, float x, bool loop, GameObject** out = nullptr)
        {
            GameObject* object = canvas->CreateObject(name);
            auto* transform = canvas->AttachComponent<Component::Transform2D>(object);
            transform->position = {x, 0.0f};
            auto* source = canvas->AttachComponent<Component::AudioSource>(object);
            source->clipId = loop ? fixture.longId : fixture.shortId;
            source->loop = loop;
            if (out != nullptr)
            {
                *out = object;
            }
            return source;
        }

        void Frame(std::uint32_t renderFrames = 800)
        {
            framework.Update(1.0f / 60.0f);
            audio.Update();
            mixer.Render(scratch, renderFrames > 800 ? 800 : renderFrames);
        }

        void Close()
        {
            framework.Shutdown();
            BindAudioSystemContext({});
            BindAudioServiceContext({});
            audio.Shutdown();
            mixer.Shutdown();
            fixture.Close();
        }

        float scratch[1600] = {};
    };

    void TestSourcesFollowTheirLifecycle()
    {
        Scene scene;
        scene.Open();
        Component::AudioSource* once = scene.AddSource("blip", 0.0f, false);
        scene.framework.BindCanvasAssets();
        Check(once->clip.generation != 0, "the clip id resolves to a handle");

        scene.Frame();
        Check(once->state == Component::AudioSourceState::Playing, "playOnStart starts the source on the first frame");
        Check(RenderPeaks(scene.mixer, 2400).left > 0.3f, "and it is heard");
        const std::uint64_t started = scene.mixer.GetStats().voicesStarted;
        // 0.1 초짜리가 끝난다. 끝난 뒤에는 다시 울리지 않는다(기존 엔진 단계 3 의 반례).
        RenderPeaks(scene.mixer, Rate / 5);
        for (int frame = 0; frame < 5; ++frame)
        {
            scene.Frame();
        }
        Check(once->state == Component::AudioSourceState::Finished, "a one-shot source finishes");
        Check(scene.mixer.GetStats().voicesStarted == started, "a finished one-shot is not started again");

        // 끄고 켜면 한 번 다시 무장된다.
        once->SetEnabled(false);
        scene.Frame();
        Check(once->state == Component::AudioSourceState::Idle, "disabling returns the source to idle");
        once->SetEnabled(true);
        scene.Frame();
        Check(once->state == Component::AudioSourceState::Playing && scene.mixer.GetStats().voicesStarted == started + 1,
            "enabling it again plays once more");

        // 루프는 멈출 때까지 운다.
        GameObject* themeObject = nullptr;
        Component::AudioSource* looping = scene.AddSource("theme", 0.0f, true, &themeObject);
        scene.framework.BindCanvasAssets();
        scene.Frame();
        RenderPeaks(scene.mixer, Rate * 2);
        scene.Frame();
        Check(looping->state == Component::AudioSourceState::Playing, "a looping source outlives its one-second clip");

        // 게임을 멈추면 소리가 멈추고, 다시 켜면 처음부터 한 번 운다.
        scene.framework.SetSimulationEnabled(false);
        Check(scene.mixer.GetStats().activeVoices == 0, "stopping the game stops every source");
        scene.Frame();
        Check(scene.mixer.GetStats().activeVoices == 0, "a stopped game does not start sources");
        scene.framework.SetSimulationEnabled(true);
        scene.Frame();
        Check(looping->state == Component::AudioSourceState::Playing, "playing again re-arms playOnStart");

        // 오브젝트를 지우면 제 보이스도 멈춘다.
        const std::uint32_t before = scene.mixer.GetStats().activeVoices;
        Check(before >= 1, "the looping source is sounding");
        scene.canvas->DestroyObject(themeObject);
        scene.canvas->FlushPendingDestroy();
        Check(scene.mixer.GetStats().activeVoices == before - 1, "destroying the object stops its voice");
        scene.Close();
    }

    void TestBusesAndSpatialSources()
    {
        Scene scene;
        scene.Open();
        Component::AudioSource* music = scene.AddSource("music", 0.0f, true);
        music->bus = AudioBusName::FromText("Music");
        scene.framework.BindCanvasAssets();
        scene.Frame();
        const float onMusic = RenderPeaks(scene.mixer, 4800).left;
        Check(std::fabs(onMusic - 0.25f) < 0.04f, "a source on the Music bus (0.5) is heard at half its level");

        // 재생 중에 버스를 바꾸면 곧바로 옮겨 간다(D-198).
        music->bus = AudioBusName::FromText("SFX");
        scene.Frame();
        Check(std::fabs(RenderPeaks(scene.mixer, 4800).left - 0.5f) < 0.05f,
            "moving it to SFX (1.0) while playing is heard at once");

        // 목록에 없는 이름은 Master 로 가고 경고는 한 번이다.
        const std::uint64_t logBefore = Log::GetRevision();
        music->bus = AudioBusName::FromText("Nowhere");
        scene.Frame();
        scene.Frame();
        Check(Log::GetRevision() == logBefore + 1, "an unknown bus warns once");
        Check(std::fabs(RenderPeaks(scene.mixer, 4800).left - 0.5f) < 0.05f, "and plays on Master");
        music->SetEnabled(false);
        scene.Frame();

        // 공간화: 리스너 오른쪽의 소스는 오른쪽이 크다. 변환을 옮기면 따라간다.
        GameObject* ear = scene.canvas->CreateObject("ear");
        scene.canvas->AttachComponent<Component::Transform2D>(ear);
        scene.canvas->AttachComponent<Component::AudioListener2D>(ear);
        GameObject* bee = nullptr;
        Component::AudioSource* buzz = scene.AddSource("bee", 8.0f, true, &bee);
        buzz->spatial = true;
        buzz->attenuation = AudioAttenuation::Linear;
        buzz->minDistance = 1.0f;
        buzz->maxDistance = 30.0f;
        scene.framework.BindCanvasAssets();
        scene.Frame();
        scene.Frame();
        const Peaks right = RenderPeaks(scene.mixer, 4800);
        Check(right.right > right.left * 1.3f, "a source to the right of the listener is louder on the right");
        scene.canvas->FindComponentRaw<Component::Transform2D>(bee)->position = {-8.0f, 0.0f};
        scene.Frame();
        scene.Frame();
        const Peaks left = RenderPeaks(scene.mixer, 4800);
        Check(left.left > left.right * 1.3f, "moving it to the left follows while it plays");
        // 가까운 소리가 한쪽 귀로 뚝 꺾이지 않는다(`panDistance`).
        scene.canvas->FindComponentRaw<Component::Transform2D>(bee)->position = {0.5f, 0.0f};
        scene.Frame();
        scene.Frame();
        const Peaks near = RenderPeaks(scene.mixer, 4800);
        Check(near.left > near.right * 0.6f, "a source half a unit away is still heard in both ears");
        scene.Close();
    }

    void TestTheScriptServiceReachesTheMixer()
    {
        Scene scene;
        scene.Open();
        Component::AudioSource* source = scene.AddSource("door", 0.0f, false);
        source->playOnStart = false;
        scene.framework.BindCanvasAssets();
        scene.Frame();
        Check(scene.mixer.GetStats().activeVoices == 0, "a source without playOnStart waits");

        const Service::AudioService& audio = GetAudioServices().Audio;
        audio.PlayOneShot(source->clip);
        Check(scene.mixer.GetStats().activeVoices == 1, "a one-shot plays through the service");
        audio.PlayOneShot(source->clip, "SFX", 0.5f);
        Check(scene.mixer.GetStats().activeVoices == 2, "a one-shot on a named bus plays");

        audio.SetBusVolume("Music", 0.25f);
        Check(std::fabs(audio.GetBusVolume("Music") - 0.25f) < 1e-6f, "a bus volume set by name reads back");
        audio.SetBusMuted("SFX", true);
        Check(audio.IsBusMuted(AudioBusName::FromText("SFX")), "a bus mutes by name");
        audio.SetBusMuted("SFX", false);

        audio.StopAll();
        Check(scene.mixer.GetStats().activeVoices == 0, "StopAll stops the game sounds");
        // 미리 듣기는 게임 소리가 아니다.
        Check(scene.audio.PlayPreview(source->clip, true), "the editor preview plays");
        audio.StopAll();
        Check(scene.audio.IsPreviewPlaying(), "StopAll leaves the editor preview alone");
        scene.audio.StopPreview();
        Check(false == scene.audio.IsPreviewPlaying(), "the preview stops");
        scene.Close();
    }

    // 에셋이 재생 중에 다시 읽히면 믹서가 먼저 그 클립을 놓는다 - 오디오 스레드가 풀린 자료를 읽지 않는다.
    void TestAReloadedClipIsReleasedBeforeItsDataGoes()
    {
        Scene scene;
        scene.Open();
        Component::AudioSource* looping = scene.AddSource("theme", 0.0f, true);
        looping->clipId = scene.fixture.shortId;
        scene.framework.BindCanvasAssets();
        scene.Frame();
        Check(scene.mixer.GetStats().activeVoices == 1 && scene.mixer.GetStats().registeredClips == 1,
            "the clip is registered and sounding");
        Check(scene.fixture.assets.ReloadInPlace(scene.fixture.shortId), "the clip reloads in place");
        Check(scene.mixer.GetStats().activeVoices == 0 && scene.mixer.GetStats().registeredClips == 0,
            "its voice stopped and its registration went before the old data was freed");
        scene.Frame();
        scene.audio.PlaySource(*looping);
        scene.Frame();
        Check(scene.mixer.GetStats().activeVoices == 1, "playing again registers the new data");
        Check(RenderPeaks(scene.mixer, 4800).left > 0.3f, "and it is heard");
        scene.Close();
    }

#if defined(_MSC_VER) && defined(_DEBUG)
    int g_allocations = 0;
    int CountAllocations(int operation, void*, std::size_t, int, long, const unsigned char*, int)
    {
        if (operation == _HOOK_ALLOC || operation == _HOOK_REALLOC)
        {
            ++g_allocations;
        }
        return 1;
    }
#endif

    // 정상 프레임은 힙을 건드리지 않는다(§9). 소스·버스·위치를 도는 오디오 몫만 잰다.
    void TestSteadyAudioFramesDoNotAllocate()
    {
        Scene scene;
        scene.Open();
        for (int index = 0; index < 8; ++index)
        {
            Component::AudioSource* source = scene.AddSource("loop", static_cast<float>(index), true);
            source->spatial = (index % 2) == 0;
            source->doppler = (index % 4) == 0 ? 1.0f : 0.0f;
            source->bus = AudioBusName::FromText((index % 3) == 0 ? "Music" : "SFX");
        }
        scene.framework.BindCanvasAssets();
        for (int frame = 0; frame < 10; ++frame)
        {
            scene.Frame();
        }
#if defined(_MSC_VER) && defined(_DEBUG)
        g_allocations = 0;
        const _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(&CountAllocations);
        const float forward[3] = {0.0f, 0.0f, -1.0f};
        const float up[3] = {0.0f, 1.0f, 0.0f};
        for (int frame = 0; frame < 120; ++frame)
        {
            const float listener[3] = {static_cast<float>(frame % 5), 0.0f, 0.0f};
            scene.audio.SetListener(listener, forward, up, 5.0f, 1.0f / 60.0f);
            scene.canvas->ForEach<Component::AudioSource>([&](Component::AudioSource& source)
            {
                const float at[3] = {static_cast<float>(frame % 7), 0.0f, 0.0f};
                scene.audio.UpdateSource(source, true, at, 1.0f / 60.0f);
            });
            scene.audio.Update();
            scene.mixer.Render(scene.scratch, 800);
        }
        _CrtSetAllocHook(previous);
        std::cout << "  CRT allocations during 120 audio frames with 8 sources: " << g_allocations << '\n';
        Check(g_allocations == 0, "a steady audio frame does not touch the heap");
#endif
        scene.Close();
    }
}

int RunAudioIntegrationTests()
{
    const bool echo = JBro::Log::GetEchoToConsole();
    JBro::Log::SetEchoToConsole(false);
    try
    {
        TestProjectFileReadsAndKeepsAudioBuses();
        TestAudioAssetsLoadAndAnnounceTheirRelease();
        TestTheDevicePullsTheMixerAndStopsWhenAsked();
        TestSourcesFollowTheirLifecycle();
        TestBusesAndSpatialSources();
        TestTheScriptServiceReachesTheMixer();
        TestAReloadedClipIsReleasedBeforeItsDataGoes();
        TestSteadyAudioFramesDoNotAllocate();
    }
    catch (const std::exception&)
    {
        JBro::Log::SetEchoToConsole(echo);
        return 1;
    }
    JBro::Log::SetEchoToConsole(echo);
    std::cout << "audio integration tests passed\n";
    return 0;
}
