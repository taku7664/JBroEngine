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
#include <cstdlib>
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

        // 부모·센드·원음 양(D-203)도 쓸 때만 적히고 되읽힌다.
        project.audioBuses.Add(ProjectAudioBus{String("Steps"), 0.5f});
        project.audioBuses.Last().parent = "Cave";
        project.audioBuses.Last().send = "Cave";
        project.audioBuses.Last().sendLevel = 0.3f;
        project.audioBuses.Last().effects.dry = 0.0f;
        Check(WriteProjectFileText(project, LegacyProject, sizeof(LegacyProject) - 1, written, error), "routing writes");
        Check(written.find("  - Name: Steps\n    Volume: 0.5\n    Parent: Cave\n    Send: Cave\n    SendLevel: 0.3\n    Dry: 0\n")
                != String::npos,
            "the parent, the send and the dry level are written after the volume");
        Check(written.find("  - Name: Cave\n    Volume: 1\n    LowPass: 800\n    ReverbMix: 0.4\n  - Name: Steps") != String::npos,
            "a bus without routing writes no routing keys");
        Check(ParseProjectFile(written.c_str(), written.size(), reread, error) && reread.audioBuses.Last().parent == "Cave"
                && reread.audioBuses.Last().send == "Cave" && reread.audioBuses.Last().sendLevel == 0.3f
                && reread.audioBuses.Last().effects.dry == 0.0f,
            "and the routing reads back");

        // 더킹(D-205)도 쓸 때만 적힌다. 기본 풀림 시간(0.3)은 적지 않는다.
        project.audioBuses.Last().duckBy = "Cave";
        project.audioBuses.Last().duckAmount = 0.6f;
        Check(WriteProjectFileText(project, LegacyProject, sizeof(LegacyProject) - 1, written, error), "ducking writes");
        Check(written.find("    DuckBy: Cave\n    DuckAmount: 0.6\n") != String::npos
                && written.find("DuckRelease") == String::npos,
            "ducking writes its trigger and amount, not the default release");
        project.audioBuses.Last().duckRelease = 1.5f;
        Check(WriteProjectFileText(project, LegacyProject, sizeof(LegacyProject) - 1, written, error)
                && ParseProjectFile(written.c_str(), written.size(), reread, error)
                && reread.audioBuses.Last().duckBy == "Cave" && reread.audioBuses.Last().duckAmount == 0.6f
                && reread.audioBuses.Last().duckRelease == 1.5f,
            "and it reads back");

        // 장치 이름과 포커스 정책(D-203)은 최상위 키다. 괄호와 한글이 든 장치 이름도 그대로 되읽힌다.
        project.audioOutputDevice = "스피커(Realtek High Definition Audio)";
        project.audioMuteWhenUnfocused = true;
        Check(WriteProjectFileText(project, LegacyProject, sizeof(LegacyProject) - 1, written, error), "audio settings write");
        Check(ParseProjectFile(written.c_str(), written.size(), reread, error)
                && reread.audioOutputDevice == "스피커(Realtek High Definition Audio)" && reread.audioMuteWhenUnfocused,
            "the output device and the focus policy read back");
        constexpr char plain[] = "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\n";
        Check(ParseProjectFile(plain, sizeof(plain) - 1, reread, error) && reread.audioOutputDevice.empty()
                && false == reread.audioMuteWhenUnfocused,
            "a file without them uses the system default and keeps the sound when unfocused");

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
        // 실제 스피커를 여닫으면 무음이어도 "딱" 소리가 난다 - 여러 세션이 시험을 거듭 돌리는 기계에서 거슬린다.
        // 그래서 `JBRO_AUDIO_DEVICE_TEST=1` 일 때만 돈다(장치 쪽을 고친 뒤에 켜고 돌린다).
        char* enabled = nullptr;
        std::size_t enabledLength = 0;
        const bool wanted = _dupenv_s(&enabled, &enabledLength, "JBRO_AUDIO_DEVICE_TEST") == 0 && enabled != nullptr
            && enabled[0] == '1';
        std::free(enabled);
        if (false == wanted)
        {
            std::cout << "  [skip] real audio device (set JBRO_AUDIO_DEVICE_TEST=1 to open the speakers)" << std::endl;
            return;
        }
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
        // 목록의 이름으로 그 장치를 연다(D-203). 없는 이름은 열지 않는다 - 기본으로 떨어질지는 호스트가 정한다.
        AudioDeviceInfo devices[16];
        const std::uint32_t deviceCount = platform.EnumerateAudioOutputs(devices, 16);
        std::cout << "  output devices on this machine: " << deviceCount << std::endl;
        Check(deviceCount >= 1, "a machine with a default device lists at least one device");
        AudioOutputDesc named;
        named.deviceName = devices[0].name;
        OwnerPtr<IAudioOutput> chosen = platform.CreateAudioOutput(named);
        Check(chosen.Get() != nullptr && std::strcmp(chosen->GetDeviceName(), devices[0].name) == 0,
            "a device opens by its listed name");
        chosen = nullptr;
        named.deviceName = "JBro no such device";
        Check(platform.CreateAudioOutput(named).Get() == nullptr, "a missing device name does not open");
        // 장치 알림(D-206): 처음 물으면 감시를 켜고 거짓이다. 플랫폼을 내리면 감시 스레드가 멈춘다(멈추지 않으면 여기서 걸린다).
        Check(false == platform.TakeAudioDevicesChanged(), "the first question starts the device watch");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        platform.TakeAudioDevicesChanged();
        Check(pulled > 0, "the device pulls frames from the mixer");
        output->Stop();
        const std::uint64_t afterStop = mixer.GetStats().renderedFrames;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        Check(mixer.GetStats().renderedFrames == afterStop, "after Stop returns the device no longer calls the mixer");
        output = nullptr;
        mixer.Shutdown();
        platform.Shutdown();
    }

    // ── 디스크 스트리밍 (D-203) ──
    bool OpenStreamForTest(void* user, const char* path, AudioFileDecoder& decoder)
    {
        return decoder.Open(static_cast<IPlatform*>(user)->OpenFileStream(path), path);
    }

    // 소리를 장치처럼 조금씩 당기되 스트리머가 따라올 틈을 준다(실시간의 약 10 배). 창마다의 최대 크기를 모은다.
    Array<float> PacedWindowPeaks(AudioMixer& mixer, std::uint32_t windows, std::uint32_t windowFrames)
    {
        Array<float> peaks;
        float buffer[480 * 2];
        for (std::uint32_t window = 0; window < windows; ++window)
        {
            float peak = 0.0f;
            for (std::uint32_t done = 0; done < windowFrames; done += 480)
            {
                mixer.Render(buffer, 480);
                for (float sample : buffer)
                {
                    peak = std::fmax(peak, std::fabs(sample));
                }
                std::this_thread::sleep_for(std::chrono::microseconds(1000));
            }
            peaks.Add(peak);
        }
        return peaks;
    }

    void TestStreamingFromDisk()
    {
        Fixture fixture;
        fixture.Open();
        // 1 초짜리 theme.wav 를 디스크 스트리밍으로 바꾼다. 경로에 한글이 있다(`JBroAudioProbe·오디오`).
        const String metaPath = Utf8(fixture.root / "Sound" / "theme.wav.jmeta");
        AssetMetaFile meta;
        AssetMetaError metaError;
        Check(LoadAssetMetaFile(fixture.platform, metaPath.c_str(), meta, metaError), "the meta reads");
        meta.audioOptions.mode = AudioImportMode::StreamFromDisk;
        // 트림(D-205)도 메타에 적힌다 - 에셋 경로로 울릴 때 절반 크기다.
        meta.audioOptions.gain = 0.5f;
        Check(SaveAssetMetaFile(fixture.platform, metaPath.c_str(), meta), "the meta takes the disk mode");
        AssetMetaFile trimmedMeta;
        Check(LoadAssetMetaFile(fixture.platform, metaPath.c_str(), trimmedMeta, metaError) && trimmedMeta.audioOptions.gain == 0.5f,
            "the trim round-trips through the meta");
        const AssetHandle theme = fixture.assets.Load(fixture.longId);
        const AudioData* data = fixture.assets.GetAudio(theme);
        Check(data != nullptr && data->pcm.IsEmpty() && data->encoded.IsEmpty() && false == data->streamPath.empty(),
            "a disk-streamed asset keeps only its path in memory");
        Check(data->frameCount == Rate && data->channels == 2 && data->sampleRate == Rate, "its format is read from the header");
        Array<float> wave;
        Check(fixture.assets.ComputeAudioPeaks(theme, 64, wave) && wave.Size() == 64 && wave[32] > 0.4f,
            "the editor can draw its waveform by streaming it once");

        AudioMixerDesc desc;
        desc.maxVoices = 8;
        desc.maxStreams = 2;
        desc.openStream = &OpenStreamForTest;
        desc.openStreamUser = &fixture.platform;
        AudioMixer mixer;
        Check(mixer.Initialize(desc), "the mixer initializes");
        AudioClipDesc clipDesc;
        clipDesc.encoding = AudioClipEncoding::File;
        clipDesc.path = data->streamPath.c_str();
        clipDesc.frameCount = data->frameCount;
        clipDesc.sampleRate = data->sampleRate;
        clipDesc.channels = data->channels;
        const AudioClipHandle clip = mixer.RegisterClip(clipDesc);
        Check(clip.IsSet(), "a file clip registers");

        // 되풀이: 1 초짜리를 3 초 동안 당겨도 빈 틈이 없다.
        AudioPlayDesc play;
        play.clip = clip;
        play.loop = true;
        const AudioVoiceHandle looping = mixer.Play(play);
        Check(looping.IsSet(), "a disk stream starts");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const Array<float> windows = PacedWindowPeaks(mixer, 30, 4800);
        float quietest = 1.0f;
        for (float peak : windows)
        {
            quietest = std::fmin(quietest, peak);
        }
        std::cout << "  disk stream over 3 s of a 1 s loop: quietest window " << quietest << ", underruns "
                  << mixer.GetStats().streamUnderruns << '\n';
        Check(quietest > 0.4f, "a looping disk stream plays across its end without a gap");
        Check(mixer.GetStats().streamUnderruns == 0 && mixer.GetStats().activeStreams == 1,
            "the streamer keeps ahead of the audio thread");

        // 위치 옮기기: 0.5 초로 옮기면 그 자리부터다.
        mixer.Seek(looping, 0.5);
        PacedWindowPeaks(mixer, 1, 480);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        PacedWindowPeaks(mixer, 1, 480);
        const double at = mixer.GetPlaybackSeconds(looping);
        std::cout << "  after seeking a disk stream to 0.5 s: " << at << " s\n";
        Check(at > 0.49 && at < 0.56, "seeking a disk stream moves its cursor");

        // 자리는 둘뿐이다: 둘째는 되고 셋째는 거절된다.
        play.loop = false;
        const AudioVoiceHandle once = mixer.Play(play);
        Check(once.IsSet(), "a second disk stream starts");
        Check(false == mixer.Play(play).IsSet(), "a third disk stream is refused when the streams are all busy");
        // 되풀이가 아닌 것은 끝나면 거둬진다.
        PacedWindowPeaks(mixer, 13, 4800);
        mixer.Update();
        Check(false == mixer.IsAlive(once) && mixer.IsAlive(looping), "a one-shot disk stream ends and is collected");

        // 에셋 경로로도 된다: 오디오 시스템이 `File` 로 등록한다.
        mixer.StopAll();
        const auto closed = std::chrono::steady_clock::now();
        while (mixer.GetStats().activeStreams != 0 && std::chrono::steady_clock::now() - closed < std::chrono::seconds(2))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Check(mixer.GetStats().activeStreams == 0, "stopped streams are closed by the streamer");
        System::AudioSystem audio;
        Check(audio.Initialize(mixer, &fixture.assets), "the audio system initializes");
        audio.PlayOneShot(theme, AudioBusName{}, 1.0f, 1.0f);
        Check(mixer.GetStats().activeStreams == 1, "a disk-streamed asset plays through the audio system");
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const float trimmedPeak = PacedWindowPeaks(mixer, 2, 4800)[1];
        Check(std::fabs(trimmedPeak - 0.25f) < 0.04f, "and it is heard at the asset's trim");
        // 흘려 읽는 중에 내려도 멈추지 않고 끝난다.
        audio.Shutdown();
        mixer.Play(play);
        mixer.Shutdown();
        fixture.assets.Release(theme);
        fixture.Close();
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

    // 프로젝트의 부모·센드가 믹서에 선다. 솔로는 버스를 다시 세워도 남고, 소스의 필터는 재생 중에 바뀐다(D-203).
    void TestRoutingFromTheProjectAndSourceFilters()
    {
        Scene scene;
        scene.Open();
        NameTable& names = NameTable::Get();
        AudioBusConfig buses[4];
        buses[0] = {names.Intern("SFX"), 0.5f};
        buses[1] = {names.Intern("Steps"), 0.5f};
        buses[1].parent = names.Intern("SFX");
        buses[1].send = names.Intern("Room");
        buses[1].sendLevel = 1.0f;
        buses[2] = {names.Intern("Room"), 1.0f};
        buses[2].effects.dry = 0.0f;
        buses[2].effects.reverbMix = 1.0f;
        // 뒤에 있는 부모는 받지 않는다 - Master 아래로 가고 알린다.
        buses[3] = {names.Intern("Early"), 1.0f};
        buses[3].parent = names.Intern("Later");
        scene.audio.ConfigureBuses({buses, 4});
        AudioMixer& mixer = scene.mixer;
        const AudioBusId sfx = AudioFirstProjectBus;
        const AudioBusId steps = AudioFirstProjectBus + 1;
        const AudioBusId room = AudioFirstProjectBus + 2;
        Check(mixer.GetBusParent(steps) == sfx, "a bus is created under the parent named in the project");
        Check(mixer.GetBusParent(AudioFirstProjectBus + 3) == AudioMasterBus, "a parent that comes later falls back to Master");
        Check(mixer.GetBusSendTarget(steps) == room && mixer.GetBusSendLevel(steps) == 1.0f,
            "a send to a bus later in the list is connected once every bus exists");

        Component::AudioSource* source = scene.AddSource("steps", 0.0f, true);
        source->bus = AudioBusName::FromText("Steps");
        scene.framework.BindCanvasAssets();
        scene.Frame();
        RenderPeaks(mixer, 9600);
        Check(std::fabs(scene.audio.GetBusPeak(AudioBusName::FromText("Steps")) - 0.25f) < 0.04f,
            "the bus meter reads the nested bus");
        Check(scene.audio.GetBusPeak(AudioBusName::FromText("Room")) > 0.01f, "the send reaches the room");

        // 소스의 저역 통과: 440 Hz 사인을 100 Hz 로 깎으면 크게 준다.
        const AudioBusName stepsName = AudioBusName::FromText("Steps");
        const float open = scene.audio.GetBusPeak(stepsName);
        source->lowPass = 100.0f;
        scene.Frame();
        RenderPeaks(mixer, 9600);
        const float muffled = scene.audio.GetBusPeak(stepsName);
        std::cout << "  a source low-pass at 100 Hz: " << open << " -> " << muffled << '\n';
        Check(muffled < open * 0.3f, "a source's low-pass cuts it while it plays");
        source->lowPass = 0.0f;
        scene.Frame();
        RenderPeaks(mixer, 9600);
        Check(std::fabs(scene.audio.GetBusPeak(stepsName) - open) < 0.03f, "clearing it restores the source");

        // 더킹은 모든 버스가 선 뒤에 잇는다(뒤에 있는 버스여도 된다).
        AudioBusConfig ducked[2];
        ducked[0] = {names.Intern("Music"), 1.0f};
        ducked[0].duckBy = names.Intern("Voice");
        ducked[0].duckAmount = 0.5f;
        ducked[1] = {names.Intern("Voice"), 1.0f};
        scene.audio.ConfigureBuses({ducked, 2});
        Check(mixer.GetBusDuckTrigger(AudioFirstProjectBus) == AudioFirstProjectBus + 1
                && mixer.GetBusDuckAmount(AudioFirstProjectBus) == 0.5f,
            "a bus ducks under a bus named later in the project");
        // 스크립트의 페이드.
        GetAudioServices().Audio.FadeBusVolume("Music", 0.25f, 0.5f);
        Check(mixer.GetBusVolume(AudioFirstProjectBus) == 0.25f, "a script fades a bus by name");
        scene.audio.ConfigureBuses({buses, 4});

        // 솔로는 저장하지 않지만 버스를 다시 세워도 이름으로 남는다.
        scene.audio.SetBusSolo(AudioBusName::FromText("Room"), true);
        scene.audio.ConfigureBuses({buses, 4});
        Check(scene.audio.IsBusSolo(AudioBusName::FromText("Room")) && mixer.IsBusSolo(room),
            "a solo survives rebuilding the buses");
        scene.audio.SetBusSolo(AudioBusName::FromText("Room"), false);
        Check(false == mixer.IsBusSolo(room), "the solo clears");
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
        TestStreamingFromDisk();
        TestSourcesFollowTheirLifecycle();
        TestBusesAndSpatialSources();
        TestTheScriptServiceReachesTheMixer();
        TestAReloadedClipIsReleasedBeforeItsDataGoes();
        TestRoutingFromTheProjectAndSourceFilters();
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
