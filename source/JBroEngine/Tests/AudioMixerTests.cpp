#include <JBro/Audio/AudioMixer.h>
#include <JBro/Core/Log.h>
#include <JBro/Types/Array.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

// 오디오 1 단계(audio-plan §3-1): 장치 없이 믹서를 당겨 결과를 잰다. 소리는 나지 않는다.
namespace
{
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

#if defined(_MSC_VER) && defined(_DEBUG)
    std::atomic<int> g_crtAllocations{0};
    int CountCrtAllocations(int operation, void*, std::size_t, int, long, const unsigned char*, int)
    {
        if (operation == _HOOK_ALLOC || operation == _HOOK_REALLOC)
        {
            g_crtAllocations.fetch_add(1, std::memory_order_relaxed);
        }
        return 1;
    }
#endif

    Array<float> MakeSine(std::uint32_t channels, float frequency, float amplitude, float seconds)
    {
        const std::uint32_t frames = static_cast<std::uint32_t>(seconds * static_cast<float>(Rate));
        Array<float> samples;
        samples.Resize(static_cast<std::size_t>(frames) * channels);
        for (std::uint32_t frame = 0; frame < frames; ++frame)
        {
            const float value = amplitude * std::sin(2.0f * Pi * frequency * static_cast<float>(frame) / static_cast<float>(Rate));
            for (std::uint32_t channel = 0; channel < channels; ++channel)
            {
                samples[static_cast<std::size_t>(frame) * channels + channel] = value;
            }
        }
        return samples;
    }

    AudioClipHandle RegisterPcm(AudioMixer& mixer, const Array<float>& samples, std::uint32_t channels)
    {
        AudioClipDesc desc;
        desc.encoding = AudioClipEncoding::Pcm;
        desc.pcm = samples.Data();
        desc.channels = channels;
        desc.sampleRate = Rate;
        desc.frameCount = samples.Size() / channels;
        return mixer.RegisterClip(desc);
    }

    struct Rendered
    {
        Array<float> samples;
        float Peak(std::size_t channel, std::size_t beginFrame = 0, std::size_t endFrame = static_cast<std::size_t>(-1)) const
        {
            float peak = 0.0f;
            const std::size_t frames = samples.Size() / 2;
            if (endFrame > frames)
            {
                endFrame = frames;
            }
            for (std::size_t frame = beginFrame; frame < endFrame; ++frame)
            {
                peak = std::fmax(peak, std::fabs(samples[frame * 2 + channel]));
            }
            return peak;
        }

        std::uint32_t ZeroCrossings(std::size_t channel) const
        {
            std::uint32_t crossings = 0;
            const std::size_t frames = samples.Size() / 2;
            for (std::size_t frame = 1; frame < frames; ++frame)
            {
                const float previous = samples[(frame - 1) * 2 + channel];
                const float current = samples[frame * 2 + channel];
                if ((previous < 0.0f) != (current < 0.0f))
                {
                    ++crossings;
                }
            }
            return crossings;
        }
    };

    Rendered Render(AudioMixer& mixer, std::uint32_t frames)
    {
        Rendered result;
        result.samples.Resize(static_cast<std::size_t>(frames) * 2);
        // 장치의 콜백처럼 작은 조각으로 당긴다.
        std::uint32_t done = 0;
        while (done < frames)
        {
            const std::uint32_t chunk = frames - done < 480 ? frames - done : 480;
            mixer.Render(result.samples.Data() + static_cast<std::size_t>(done) * 2, chunk);
            done += chunk;
        }
        return result;
    }

    AudioMixerDesc SmallDesc(std::uint32_t voices = 8)
    {
        AudioMixerDesc desc;
        desc.sampleRate = Rate;
        desc.channels = 2;
        desc.maxVoices = voices;
        desc.maxClips = 16;
        return desc;
    }

    void TestSilenceWithoutVoices()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc()), "mixer initializes without a device");
        const Rendered out = Render(mixer, 4800);
        Check(out.Peak(0) == 0.0f && out.Peak(1) == 0.0f, "no voices renders silence");
        mixer.Shutdown();

        // 초기화 전과 뒤에도 Render 는 0 을 채우고 죽지 않는다.
        AudioMixer idle;
        Rendered idleOut;
        idleOut.samples.Resize(960);
        idleOut.samples[0] = 5.0f;
        idle.Render(idleOut.samples.Data(), 480);
        Check(idleOut.samples[0] == 0.0f, "an uninitialized mixer renders zeros");
    }

    void TestVolumeAndBuses()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc()), "mixer initializes");
        const Array<float> sine = MakeSine(2, 440.0f, 0.5f, 1.0f);
        const AudioClipHandle clip = RegisterPcm(mixer, sine, 2);
        Check(clip.IsSet(), "a PCM clip registers");

        AudioPlayDesc play;
        play.clip = clip;
        AudioVoiceHandle voice = mixer.Play(play);
        Check(voice.IsSet() && mixer.IsAlive(voice), "a voice starts");
        Rendered out = Render(mixer, 4800);
        const float fullPeak = out.Peak(0);
        Check(std::fabs(fullPeak - 0.5f) < 0.05f, "a stereo clip at unit gain renders at its own amplitude");
        Check(std::fabs(out.Peak(1) - 0.5f) < 0.05f, "both channels carry the clip");

        mixer.SetVolume(voice, 0.5f);
        out = Render(mixer, 4800);
        Check(std::fabs(out.Peak(0, 2400) - 0.25f) < 0.04f, "voice volume scales the output");

        mixer.SetVolume(voice, 1.0f);
        const AudioBusId music = mixer.CreateBus(0.5f);
        Check(music >= AudioFirstProjectBus, "a project bus is created under the master");
        mixer.SetBus(voice, music);
        Check(mixer.IsAlive(voice), "moving a playing voice to another bus keeps it alive");
        out = Render(mixer, 4800);
        Check(std::fabs(out.Peak(0, 2400) - 0.25f) < 0.04f, "the bus volume applies after rerouting while playing");

        mixer.SetBusMuted(music, true);
        out = Render(mixer, 4800);
        Check(out.Peak(0, 2400) < 0.001f, "a muted bus is silent");
        Check(mixer.IsBusMuted(music) && std::fabs(mixer.GetBusVolume(music) - 0.5f) < 1e-6f,
            "muting keeps the stored bus volume");
        mixer.SetBusMuted(music, false);

        mixer.SetBusVolume(AudioMasterBus, 0.5f);
        out = Render(mixer, 4800);
        Check(std::fabs(out.Peak(0, 2400) - 0.125f) < 0.03f, "the master bus multiplies every project bus");
        mixer.SetBusVolume(AudioMasterBus, 1.0f);

        mixer.DestroyProjectBuses();
        Check(mixer.IsAlive(voice), "destroying project buses moves their voices to the master");
        out = Render(mixer, 4800);
        Check(std::fabs(out.Peak(0, 2400) - 0.5f) < 0.05f, "a voice moved to the master plays at full volume");

        // 원자 변수가 아닌 값은 재생 중에 쓰지 않는다 - 그 API 자체가 없다(D-198). 그래서 여기서는 Stop 만 본다.
        mixer.Stop(voice);
        Check(false == mixer.IsAlive(voice), "stop releases the voice at once");
        mixer.SetVolume(voice, 1.0f);
        mixer.Stop(voice);
        Check(mixer.GetStats().activeVoices == 0, "calls on a dead handle do nothing");
        mixer.Shutdown();
    }

    void TestPitchLoopAndEnd()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc()), "mixer initializes");
        const Array<float> sine = MakeSine(2, 440.0f, 0.5f, 0.1f);
        const AudioClipHandle clip = RegisterPcm(mixer, sine, 2);

        AudioPlayDesc play;
        play.clip = clip;
        play.loop = true;
        AudioVoiceHandle normal = mixer.Play(play);
        Rendered out = Render(mixer, 9600);
        const std::uint32_t normalCrossings = out.ZeroCrossings(0);
        mixer.Stop(normal);

        play.pitch = 2.0f;
        AudioVoiceHandle doubled = mixer.Play(play);
        out = Render(mixer, 9600);
        const std::uint32_t doubledCrossings = out.ZeroCrossings(0);
        std::cout << "  pitch 1: " << normalCrossings << " crossings, pitch 2: " << doubledCrossings << '\n';
        Check(normalCrossings > 170 && normalCrossings < 182, "440 Hz crosses zero about 176 times in 0.2 s");
        Check(doubledCrossings > normalCrossings * 2 - 8 && doubledCrossings < normalCrossings * 2 + 8,
            "pitch 2 doubles the frequency");
        Check(mixer.IsAlive(doubled), "a looping voice outlives its clip length");
        Check(out.Peak(0, 8000) > 0.4f, "a looping voice keeps sounding across the loop boundary");
        mixer.Stop(doubled);

        play.pitch = 1.0f;
        play.loop = false;
        AudioVoiceHandle once = mixer.Play(play);
        out = Render(mixer, 9600);
        mixer.Update();
        Check(false == mixer.IsAlive(once), "a one-shot voice ends and Update reclaims its slot");
        Check(out.Peak(0, 4900) < 0.001f, "nothing sounds after a one-shot clip ends");
        Check(mixer.GetStats().activeVoices == 0, "the slot went back to the pool");
        mixer.Shutdown();
    }

    void TestDelayAndFade()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc()), "mixer initializes");
        const Array<float> sine = MakeSine(2, 440.0f, 0.5f, 1.0f);
        const AudioClipHandle clip = RegisterPcm(mixer, sine, 2);

        AudioPlayDesc play;
        play.clip = clip;
        play.startDelaySeconds = 0.05f;
        mixer.Play(play);
        Rendered out = Render(mixer, 9600);
        Check(out.Peak(0, 0, 2300) < 0.001f, "a delayed voice is silent before its start time");
        Check(out.Peak(0, 2500, 9600) > 0.4f, "a delayed voice sounds after its start time");
        mixer.StopAll();

        play.startDelaySeconds = 0.0f;
        play.fadeInSeconds = 0.1f;
        mixer.Play(play);
        out = Render(mixer, 9600);
        Check(out.Peak(0, 0, 480) < 0.1f, "a fade-in starts quiet");
        Check(out.Peak(0, 6000, 9600) > 0.4f, "a fade-in reaches full volume");
        mixer.Shutdown();
    }

    void TestSpatialization()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc()), "mixer initializes");
        const Array<float> sine = MakeSine(1, 440.0f, 0.5f, 1.0f);
        const AudioClipHandle clip = RegisterPcm(mixer, sine, 1);
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        const float forward[3] = {0.0f, 0.0f, -1.0f};
        const float up[3] = {0.0f, 1.0f, 0.0f};
        mixer.SetListener(origin, forward, up);

        AudioPlayDesc play;
        play.clip = clip;
        play.spatial = true;
        play.attenuation = AudioAttenuation::Linear;
        play.minDistance = 1.0f;
        play.maxDistance = 20.0f;
        play.position[0] = 5.0f;
        AudioVoiceHandle right = mixer.Play(play);
        Rendered out = Render(mixer, 4800);
        Check(out.Peak(1, 2400) > out.Peak(0, 2400) * 1.5f, "a source to the right of the listener is louder on the right");
        const float nearPeak = out.Peak(1, 2400);

        const float far[3] = {15.0f, 0.0f, 0.0f};
        mixer.SetPosition(right, far);
        out = Render(mixer, 4800);
        Check(out.Peak(1, 2400) < nearPeak * 0.8f, "moving the source away attenuates it");

        const float beyond[3] = {40.0f, 0.0f, 0.0f};
        mixer.SetPosition(right, beyond);
        out = Render(mixer, 4800);
        Check(out.Peak(1, 2400) < 0.01f, "linear attenuation reaches silence past the maximum distance");
        mixer.Stop(right);

        play.spatial = false;
        mixer.Play(play);
        out = Render(mixer, 4800);
        Check(std::fabs(out.Peak(0, 2400) - out.Peak(1, 2400)) < 0.01f, "a non-spatial mono clip plays centred");
        mixer.Shutdown();
    }

    // 16 비트 PCM WAV 를 메모리에 짓는다. 디코더 경로(Streaming)를 파일 없이 본다.
    Array<std::uint8_t> MakeWav(std::uint32_t frames, float frequency, float amplitude)
    {
        Array<std::uint8_t> bytes;
        const std::uint32_t dataBytes = frames * 2 * 2;
        bytes.Resize(44 + dataBytes);
        auto put32 = [&](std::size_t at, std::uint32_t value)
        {
            std::memcpy(bytes.Data() + at, &value, 4);
        };
        auto put16 = [&](std::size_t at, std::uint16_t value)
        {
            std::memcpy(bytes.Data() + at, &value, 2);
        };
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
            const float value = amplitude * std::sin(2.0f * Pi * frequency * static_cast<float>(frame) / static_cast<float>(Rate));
            const std::int16_t sample = static_cast<std::int16_t>(value * 32767.0f);
            std::memcpy(bytes.Data() + 44 + frame * 4, &sample, 2);
            std::memcpy(bytes.Data() + 44 + frame * 4 + 2, &sample, 2);
        }
        return bytes;
    }

    void TestEncodedClip()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc()), "mixer initializes");
        const Array<std::uint8_t> wav = MakeWav(Rate / 2, 440.0f, 0.5f);
        AudioClipDesc desc;
        desc.encoding = AudioClipEncoding::Encoded;
        desc.bytes = wav.Data();
        desc.byteCount = wav.Size();
        desc.frameCount = Rate / 2;
        desc.sampleRate = Rate;
        desc.channels = 2;
        const AudioClipHandle clip = mixer.RegisterClip(desc);
        Check(clip.IsSet(), "an encoded clip registers");
        Check(std::fabs(mixer.GetClipDurationSeconds(clip) - 0.5) < 1e-6, "an encoded clip reports its length");

        AudioPlayDesc play;
        play.clip = clip;
        AudioVoiceHandle voice = mixer.Play(play);
        Check(voice.IsSet(), "an encoded clip plays through a decoder");
        Rendered out = Render(mixer, 4800);
        Check(std::fabs(out.Peak(0) - 0.5f) < 0.05f, "the decoder streams the WAV at its amplitude");
        Check(mixer.GetPlaybackSeconds(voice) > 0.09, "the cursor advances as the decoder streams");
        mixer.UnregisterClip(clip);
        Check(false == mixer.IsAlive(voice), "unregistering a clip stops its voices");
        Check(false == mixer.Play(play).IsSet(), "a dead clip does not play");
        mixer.Shutdown();
    }

    void TestSeekWhilePlaying()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc()), "mixer initializes");
        const Array<float> sine = MakeSine(2, 440.0f, 0.5f, 2.0f);
        const AudioClipHandle clip = RegisterPcm(mixer, sine, 2);
        AudioPlayDesc play;
        play.clip = clip;
        AudioVoiceHandle voice = mixer.Play(play);
        Render(mixer, 4800);
        mixer.Seek(voice, 1.5);
        Render(mixer, 4800);
        const double at = mixer.GetPlaybackSeconds(voice);
        Check(at > 1.5 && at < 1.7, "seeking while playing moves the cursor");
        mixer.Seek(voice, 100.0);
        Render(mixer, 480);
        Check(mixer.GetPlaybackSeconds(voice) < 2.01, "a seek past the end clamps to the clip");
        mixer.Shutdown();
    }

    // 이펙트 사슬(D-202): 저역 통과가 고음을, 고역 통과가 저음을 깎고, 메아리가 간격 뒤에 되울리고, 잔향이 소리가 멎은
    // 뒤에도 꼬리를 남긴다. 켜 둔 채로 도는 정상 프레임은 힙을 건드리지 않는다.
    void TestBusEffects()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc()), "mixer initializes");
        const Array<float> high = MakeSine(2, 5000.0f, 0.5f, 1.0f);
        const Array<float> low = MakeSine(2, 100.0f, 0.5f, 1.0f);
        const AudioClipHandle highClip = RegisterPcm(mixer, high, 2);
        const AudioClipHandle lowClip = RegisterPcm(mixer, low, 2);
        const AudioBusId bus = mixer.CreateBus(1.0f);

        AudioPlayDesc play;
        play.clip = highClip;
        play.bus = bus;
        play.loop = true;
        AudioVoiceHandle voice = mixer.Play(play);
        const float dry = Render(mixer, 9600).Peak(0, 4800);
        AudioBusEffects effects;
        effects.lowPassHz = 300.0f;
        mixer.SetBusEffects(bus, effects);
        const float cut = Render(mixer, 9600).Peak(0, 4800);
        std::cout << "  5 kHz through a 300 Hz low-pass: " << dry << " -> " << cut << '\n';
        Check(cut < dry * 0.1f, "the low-pass cuts a tone far above it");
        effects.lowPassHz = 0.0f;
        mixer.SetBusEffects(bus, effects);
        Check(std::fabs(Render(mixer, 9600).Peak(0, 4800) - dry) < 0.05f, "zero turns the low-pass off while playing");
        mixer.Stop(voice);

        play.clip = lowClip;
        voice = mixer.Play(play);
        effects.highPassHz = 3000.0f;
        mixer.SetBusEffects(bus, effects);
        Check(Render(mixer, 9600).Peak(0, 4800) < 0.05f, "the high-pass cuts a tone far below it");
        effects.highPassHz = 0.0f;
        mixer.SetBusEffects(bus, effects);
        mixer.Stop(voice);

        // 메아리: 0.05 초 소리 뒤 0.2 초에 되울린다.
        const Array<float> burst = MakeSine(2, 440.0f, 0.5f, 0.05f);
        const AudioClipHandle burstClip = RegisterPcm(mixer, burst, 2);
        play.clip = burstClip;
        play.loop = false;
        effects.echoMix = 0.5f;
        effects.echoDelay = 0.2f;
        effects.echoFeedback = 0.0f;
        mixer.SetBusEffects(bus, effects);
        Render(mixer, 480);
        mixer.Play(play);
        const Rendered echoed = Render(mixer, Rate / 2);
        const float first = echoed.Peak(0, 0, 2400);
        const float gap = echoed.Peak(0, 3500, 9000);
        const float repeat = echoed.Peak(0, 9600, 12500);
        std::cout << "  echo: first " << first << ", gap " << gap << ", repeat " << repeat << '\n';
        Check(first > 0.4f && gap < 0.01f && repeat > 0.15f, "the echo repeats the burst after its delay");
        effects.echoMix = 0.0f;
        mixer.SetBusEffects(bus, effects);
        Render(mixer, Rate);

        // 잔향: 소리가 멎은 뒤에도 꼬리가 운다. 끄면 꼬리가 없다.
        mixer.Play(play);
        const Rendered plain = Render(mixer, Rate / 2);
        Check(plain.Peak(0, 4800, 24000) < 0.001f, "without reverb nothing sounds after the burst");
        effects.reverbMix = 0.5f;
        mixer.SetBusEffects(bus, effects);
        mixer.Play(play);
        const Rendered wet = Render(mixer, Rate / 2);
        std::cout << "  reverb tail after the burst: " << wet.Peak(0, 4800, 24000) << '\n';
        Check(wet.Peak(0, 4800, 24000) > 0.005f, "with reverb a tail rings after the burst");

        // 켜 둔 채 도는 프레임은 할당이 없다(버퍼는 처음 켤 때 잡았다).
        effects.lowPassHz = 2000.0f;
        effects.highPassHz = 80.0f;
        effects.echoMix = 0.3f;
        mixer.SetBusEffects(bus, effects);
        play.loop = true;
        mixer.Play(play);
        Array<float> buffer;
        buffer.Resize(960);
#if defined(_MSC_VER) && defined(_DEBUG)
        g_crtAllocations = 0;
        _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(&CountCrtAllocations);
#endif
        for (int frame = 0; frame < 200; ++frame)
        {
            effects.lowPassHz = 1000.0f + static_cast<float>(frame * 10);
            mixer.SetBusEffects(bus, effects);
            mixer.Render(buffer.Data(), 480);
            mixer.Update();
        }
#if defined(_MSC_VER) && defined(_DEBUG)
        _CrtSetAllocHook(previous);
        Check(g_crtAllocations.load() == 0, "frames with every effect on do not touch the heap");
#endif
        Check(mixer.GetBusEffects(bus).lowPassHz == effects.lowPassHz, "the bus reports the effects it was given");
        mixer.Shutdown();
    }

    // 다른 스레드가 당기는 동안 이펙트를 거듭 켜고 끈다. 값이 찢기거나 풀린 버퍼를 읽으면 NaN 이나 큰 값이 나온다.
    void TestEffectsChangeWhileRendering()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc()), "mixer initializes");
        const Array<float> sine = MakeSine(2, 440.0f, 0.3f, 1.0f);
        const AudioClipHandle clip = RegisterPcm(mixer, sine, 2);
        const AudioBusId bus = mixer.CreateBus(1.0f);
        AudioPlayDesc play;
        play.clip = clip;
        play.bus = bus;
        play.loop = true;
        mixer.Play(play);
        std::atomic<bool> running{true};
        std::atomic<bool> bad{false};
        std::thread audio([&]
        {
            float buffer[480 * 2];
            while (running.load(std::memory_order_acquire))
            {
                mixer.Render(buffer, 480);
                for (float sample : buffer)
                {
                    if (!std::isfinite(sample))
                    {
                        bad.store(true, std::memory_order_relaxed);
                    }
                }
            }
        });
        AudioBusEffects effects;
        for (int round = 0; round < 2000; ++round)
        {
            effects.lowPassHz = (round % 3) == 0 ? 0.0f : 200.0f + static_cast<float>(round % 17) * 500.0f;
            effects.highPassHz = (round % 5) == 0 ? 0.0f : 50.0f + static_cast<float>(round % 7) * 100.0f;
            effects.echoMix = (round % 2) == 0 ? 0.0f : 0.4f;
            effects.echoDelay = 0.01f + static_cast<float>(round % 11) * 0.15f;
            effects.reverbMix = (round % 4) == 0 ? 0.0f : 0.3f;
            mixer.SetBusEffects(bus, effects);
        }
        running.store(false, std::memory_order_release);
        audio.join();
        Check(false == bad.load(), "changing effects while the audio thread renders never produces a torn sample");
        mixer.Shutdown();
    }

    // 버스 중첩·센드·솔로·원음 양·미터(audio-plan §3-7).
    void TestBusRouting()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc()), "mixer initializes");
        const Array<float> sine = MakeSine(2, 440.0f, 0.5f, 1.0f);
        const AudioClipHandle clip = RegisterPcm(mixer, sine, 2);

        // 중첩: SFX(0.5) 아래 Footsteps(0.5) 의 소리는 둘을 곱한 0.25 배다.
        const AudioBusId sfx = mixer.CreateBus(0.5f);
        const AudioBusId footsteps = mixer.CreateBus(0.5f, sfx);
        Check(mixer.GetBusParent(footsteps) == sfx && mixer.GetBusParent(sfx) == AudioMasterBus, "a bus remembers its parent");
        Check(mixer.GetBusParent(mixer.CreateBus(1.0f, AudioEditorPreviewBus)) == AudioMasterBus,
            "the preview bus cannot be a parent");
        AudioPlayDesc play;
        play.clip = clip;
        play.loop = true;
        play.bus = footsteps;
        const AudioVoiceHandle step = mixer.Play(play);
        float peak = Render(mixer, 9600).Peak(0, 4800);
        Check(std::fabs(peak - 0.125f) < 0.02f, "a nested bus multiplies its parent's volume");
        Check(std::fabs(mixer.GetBusPeak(footsteps) - 0.25f) < 0.03f && std::fabs(mixer.GetBusPeak(sfx) - 0.125f) < 0.02f,
            "each bus meters its own output");
        mixer.SetBusMuted(sfx, true);
        Check(Render(mixer, 9600).Peak(0, 4800) < 0.001f, "muting a parent silences its children");
        mixer.SetBusMuted(sfx, false);

        // 센드: Footsteps 가 원음 0 의 Reverb 버스로 보낸다. 센드를 끊으면 Reverb 는 조용하다.
        const AudioBusId reverb = mixer.CreateBus(1.0f);
        AudioBusEffects wetOnly;
        wetOnly.dry = 0.0f;
        wetOnly.reverbMix = 1.0f;
        mixer.SetBusEffects(reverb, wetOnly);
        Render(mixer, 4800);
        Check(mixer.GetBusPeak(reverb) == 0.0f, "a bus nothing sends to stays silent");
        Check(mixer.SetBusSend(footsteps, reverb, 1.0f), "a send to an unrelated bus is accepted");
        Render(mixer, 9600);
        const float returned = mixer.GetBusPeak(reverb);
        std::cout << "  reverb return fed by a send: " << returned << '\n';
        Check(returned > 0.01f, "the send feeds the return bus");
        Check(mixer.GetBusSendTarget(footsteps) == reverb && mixer.GetBusSendLevel(footsteps) == 1.0f,
            "the bus reports its send");
        Check(false == mixer.SetBusSend(reverb, footsteps, 1.0f), "a send that would loop back is refused");
        Check(false == mixer.SetBusSend(sfx, footsteps, 1.0f), "a parent cannot send into its own child");
        Check(false == mixer.SetBusSend(footsteps, footsteps, 1.0f), "a bus cannot send to itself");
        Check(false == mixer.SetBusSend(AudioMasterBus, reverb, 1.0f), "the master cannot send");
        Check(mixer.GetBusSendTarget(footsteps) == reverb, "a refused send keeps the previous one");
        Check(mixer.SetBusSend(footsteps, AudioNoBus, 0.0f), "a send can be cut");
        Render(mixer, Rate * 2);
        Check(mixer.GetBusPeak(reverb) < 0.01f, "a cut send stops feeding the return bus");

        // 원음 양: 0 이면 이펙트가 꺼진 버스는 조용하다.
        AudioBusEffects muted;
        muted.dry = 0.0f;
        mixer.SetBusEffects(footsteps, muted);
        Check(Render(mixer, 9600).Peak(0, 4800) < 0.001f, "dry 0 without any wet effect is silent");
        mixer.SetBusEffects(footsteps, AudioBusEffects{});

        // 솔로: Music 을 솔로로 두면 Footsteps 는 들리지 않고, 풀면 전과 같다.
        const AudioBusId music = mixer.CreateBus(1.0f);
        play.bus = music;
        play.volume = 0.2f;
        mixer.Play(play);
        const float both = Render(mixer, 9600).Peak(0, 4800);
        mixer.SetBusSolo(music, true);
        const float soloed = Render(mixer, 9600).Peak(0, 4800);
        Check(std::fabs(soloed - 0.1f) < 0.02f, "a solo keeps only the soloed bus");
        Check(mixer.IsBusSolo(music) && false == mixer.IsBusMuted(footsteps) && mixer.GetBusVolume(footsteps) == 0.5f,
            "a solo leaves the other buses' own settings alone");
        mixer.SetBusSolo(music, false);
        mixer.SetBusSolo(footsteps, true);
        Check(std::fabs(Render(mixer, 9600).Peak(0, 4800) - 0.125f) < 0.02f, "a soloed child keeps its parent open");
        mixer.SetBusSolo(footsteps, false);
        Check(std::fabs(Render(mixer, 9600).Peak(0, 4800) - both) < 0.02f, "clearing the solo restores the mix");
        Check(mixer.IsAlive(step), "routing changes never stop a voice");
        mixer.Shutdown();
    }

    // 보이스 필터: 한 보이스만 먹먹하게 하고, 끄면 필터 노드를 떠난다(D-203).
    void TestVoiceFilter()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc()), "mixer initializes");
        const Array<float> high = MakeSine(2, 5000.0f, 0.5f, 1.0f);
        const AudioClipHandle clip = RegisterPcm(mixer, high, 2);
        AudioPlayDesc play;
        play.clip = clip;
        play.loop = true;
        play.lowPassHz = 300.0f;
        const AudioVoiceHandle muffled = mixer.Play(play);
        const float cut = Render(mixer, 9600).Peak(0, 4800);
        std::cout << "  5 kHz voice through its own 300 Hz low-pass: " << cut << '\n';
        Check(cut < 0.05f, "a voice filter set at Play cuts the voice");
        play.lowPassHz = 0.0f;
        const AudioVoiceHandle clear = mixer.Play(play);
        Check(std::fabs(Render(mixer, 9600).Peak(0, 4800) - 0.5f) < 0.08f, "another voice on the same bus is untouched");
        mixer.Stop(clear);
        mixer.SetVoiceFilter(muffled, 0.0f, 0.0f);
        Check(std::fabs(Render(mixer, 9600).Peak(0, 4800) - 0.5f) < 0.08f, "clearing the filter restores the voice");
        mixer.SetVoiceFilter(muffled, 0.0f, 20000.0f);
        Check(Render(mixer, 9600).Peak(0, 4800) < 0.1f, "a high-pass set while playing cuts the voice");

        // 켜고 끄기를 거듭해도 할당이 없다(노드는 초기화 때 만들었다).
        Array<float> buffer;
        buffer.Resize(960);
#if defined(_MSC_VER) && defined(_DEBUG)
        g_crtAllocations = 0;
        _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(&CountCrtAllocations);
#endif
        for (int frame = 0; frame < 200; ++frame)
        {
            mixer.SetVoiceFilter(muffled, (frame % 3) == 0 ? 0.0f : 500.0f + static_cast<float>(frame), 0.0f);
            mixer.Render(buffer.Data(), 480);
        }
#if defined(_MSC_VER) && defined(_DEBUG)
        _CrtSetAllocHook(previous);
        Check(g_crtAllocations.load() == 0, "toggling a voice filter does not touch the heap");
#endif
        mixer.Stop(muffled);
        // 필터를 쓴 자리를 새 보이스가 받으면 필터 없이 시작한다.
        const AudioVoiceHandle fresh = mixer.Play(play);
        Check(std::fabs(Render(mixer, 9600).Peak(0, 4800) - 0.5f) < 0.08f, "a recycled voice does not inherit a filter");
        mixer.Stop(fresh);
        mixer.Shutdown();
    }

    // 출력 이득은 곧게 옮겨 가고(클릭 없음), 스펙트럼은 사인파의 자리를 가리킨다.
    void TestOutputGainAndSpectrum()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc()), "mixer initializes");
        const Array<float> sine = MakeSine(2, 1000.0f, 0.5f, 1.0f);
        const AudioClipHandle clip = RegisterPcm(mixer, sine, 2);
        AudioPlayDesc play;
        play.clip = clip;
        play.loop = true;
        mixer.Play(play);
        Render(mixer, 4800);
        mixer.SetOutputGain(0.0f, 0.1f);
        const Rendered fading = Render(mixer, Rate / 5);
        float largestJump = 0.0f;
        for (std::size_t frame = 1; frame < fading.samples.Size() / 2; ++frame)
        {
            largestJump = std::fmax(largestJump, std::fabs(fading.samples[frame * 2] - fading.samples[(frame - 1) * 2]));
        }
        const float halfway = fading.Peak(0, 2000, 2600);
        std::cout << "  output gain fade: halfway " << halfway << ", after " << fading.Peak(0, 5000) << '\n';
        Check(halfway > 0.1f && halfway < 0.45f, "the output gain is still on its way halfway through the fade");
        Check(fading.Peak(0, 5000) < 0.001f, "the output gain reaches zero after its fade");
        // 1 kHz 사인의 한 샘플 차는 최대 2πf/rate × 0.5 ≈ 0.065 다. 뚝 끊기면 0.5 가까이 뛴다.
        Check(largestJump < 0.08f, "fading the output never jumps");
        mixer.SetOutputGain(1.0f, 0.0f);
        Render(mixer, 4800);

        float bands[32];
        mixer.ComputeSpectrum(bands, 32);
        std::uint32_t loudest = 0;
        for (std::uint32_t band = 1; band < 32; ++band)
        {
            if (bands[band] > bands[loudest])
            {
                loudest = band;
            }
        }
        const float from = 30.0f * std::pow(24000.0f / 30.0f, static_cast<float>(loudest) / 32.0f);
        const float to = 30.0f * std::pow(24000.0f / 30.0f, static_cast<float>(loudest + 1) / 32.0f);
        std::cout << "  spectrum: loudest band " << loudest << " (" << from << ".." << to << " Hz) = " << bands[loudest] << '\n';
        Check(from <= 1050.0f && to >= 950.0f, "the loudest spectrum band holds the 1 kHz tone");
        // 0.5 는 -6 dB 이므로 (72 - 6) / 72 ≈ 0.92 다.
        Check(bands[loudest] > 0.85f && bands[loudest] < 0.97f, "the spectrum reads the tone's level in decibels");
        Check(bands[31] < bands[loudest] - 0.3f, "bands far from the tone are much quieter");
        float recent[64];
        Check(mixer.CopyRecentOutput(recent, 64) == 64, "the recent output can be copied");
        mixer.Shutdown();
    }

    float LargestJump(const Rendered& out)
    {
        float largest = 0.0f;
        for (std::size_t frame = 1; frame < out.samples.Size() / 2; ++frame)
        {
            largest = std::fmax(largest, std::fabs(out.samples[frame * 2] - out.samples[(frame - 1) * 2]));
        }
        return largest;
    }

    // 버스 음량 페이드·클릭 없는 음소거·더킹·클립 트림(D-205).
    void TestBusFadesDuckingAndTrim()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc()), "mixer initializes");
        const Array<float> sine = MakeSine(2, 440.0f, 0.5f, 1.0f);
        const AudioClipHandle clip = RegisterPcm(mixer, sine, 2);
        const AudioBusId music = mixer.CreateBus(1.0f);
        AudioPlayDesc play;
        play.clip = clip;
        play.loop = true;
        play.bus = music;
        mixer.Play(play);
        Render(mixer, 4800);

        // 0.1 초 페이드: 중간은 절반쯤이고 끝은 0 이며 뛰지 않는다.
        mixer.SetBusVolume(music, 0.0f, 0.1f);
        const Rendered fading = Render(mixer, Rate / 5);
        const float halfway = fading.Peak(0, 2000, 2800);
        std::cout << "  bus fade: halfway " << halfway << ", after " << fading.Peak(0, 5200) << '\n';
        Check(halfway > 0.15f && halfway < 0.35f, "a bus volume fade is halfway at half its time");
        Check(fading.Peak(0, 5200) < 0.001f, "and silent at its end");
        // 440 Hz 사인의 한 샘플 차는 최대 약 0.03 이다. 뚝 바뀌면 0.5 가까이 뛴다.
        Check(LargestJump(fading) < 0.05f, "a bus fade never jumps");
        Check(mixer.GetBusVolume(music) == 0.0f, "the bus reports the volume it fades to");
        mixer.SetBusVolume(music, 1.0f);
        Render(mixer, 4800);

        // 음소거·솔로도 10 ms 에 걸쳐 바뀐다 - 클릭이 없다.
        mixer.SetBusMuted(music, true);
        const Rendered muting = Render(mixer, 4800);
        Check(LargestJump(muting) < 0.05f && muting.Peak(0, 960) < 0.001f, "muting ramps over about 10 ms without a click");
        mixer.SetBusMuted(music, false);
        Render(mixer, 4800);

        // 더킹: Voice 에 소리가 있는 동안 Music 이 절반으로 물러서고, 끝나면 돌아온다.
        const AudioBusId voice = mixer.CreateBus(1.0f);
        mixer.SetBusDucking(music, voice, 0.5f, 0.1f);
        Check(mixer.GetBusDuckTrigger(music) == voice && mixer.GetBusDuckAmount(music) == 0.5f, "the bus reports its ducking");
        const Array<float> line = MakeSine(2, 1000.0f, 0.3f, 0.3f);
        const AudioClipHandle lineClip = RegisterPcm(mixer, line, 2);
        AudioPlayDesc speak;
        speak.clip = lineClip;
        speak.bus = voice;
        mixer.Play(speak);
        Render(mixer, 9600);
        const float ducked = mixer.GetBusPeak(music);
        Render(mixer, Rate / 2);
        mixer.Update();
        const float recovered = mixer.GetBusPeak(music);
        std::cout << "  ducking: music under a line " << ducked << ", after it " << recovered << '\n';
        Check(std::fabs(ducked - 0.25f) < 0.04f, "a ducked bus steps back by its amount while the trigger sounds");
        Check(std::fabs(recovered - 0.5f) < 0.04f, "and comes back after the trigger stops");
        mixer.SetBusDucking(music, music, 0.5f, 0.1f);
        Check(mixer.GetBusDuckTrigger(music) == AudioNoBus, "a bus cannot duck under itself");
        mixer.SetBusDucking(music, AudioNoBus, 0.0f, 0.0f);
        Check(mixer.GetBusDuckTrigger(music) == AudioNoBus, "ducking turns off");

        // 트림: 클립의 크기 보정이 보이스 음량에 곱해진다.
        mixer.StopAll();
        AudioClipDesc trimmed;
        trimmed.encoding = AudioClipEncoding::Pcm;
        trimmed.pcm = sine.Data();
        trimmed.channels = 2;
        trimmed.sampleRate = Rate;
        trimmed.frameCount = sine.Size() / 2;
        trimmed.gain = 0.5f;
        AudioPlayDesc quiet;
        quiet.clip = mixer.RegisterClip(trimmed);
        quiet.loop = true;
        const AudioVoiceHandle trimmedVoice = mixer.Play(quiet);
        Check(std::fabs(Render(mixer, 9600).Peak(0, 4800) - 0.25f) < 0.03f, "a clip's trim scales its voices");
        mixer.SetVolume(trimmedVoice, 0.5f);
        Check(std::fabs(Render(mixer, 9600).Peak(0, 4800) - 0.125f) < 0.02f, "the trim stays under a changed voice volume");
        mixer.Shutdown();
    }

    // 버스 사용자 처리기(D-206): 사슬에 끼어 소리를 고치고, 떼고 돌아온 뒤에는 오디오 스레드가 다시 부르지 않는다.
    struct HalfGain
    {
        std::atomic<int> calls{0};
        static void Process(void* user, float* frames, std::uint32_t frameCount, std::uint32_t channels, std::uint32_t)
        {
            HalfGain* self = static_cast<HalfGain*>(user);
            self->calls.fetch_add(1, std::memory_order_relaxed);
            for (std::uint32_t index = 0; index < frameCount * channels; ++index)
            {
                frames[index] *= 0.5f;
            }
        }
    };

    // 처리 한 번이 오래 걸리는 처리기다. 불리는 동안 `active` 가 참이다 - 떼기가 기다리지 않으면 돌아온 뒤에도 참으로 보인다.
    struct SlowProbe
    {
        std::atomic<bool> active{false};
        std::atomic<int> calls{0};
        static void Process(void* user, float*, std::uint32_t, std::uint32_t, std::uint32_t)
        {
            SlowProbe* self = static_cast<SlowProbe*>(user);
            self->active.store(true, std::memory_order_seq_cst);
            self->calls.fetch_add(1, std::memory_order_relaxed);
            const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(300);
            while (std::chrono::steady_clock::now() < until)
            {
            }
            self->active.store(false, std::memory_order_seq_cst);
        }
    };

    void TestBusProcessor()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc()), "mixer initializes");
        const Array<float> sine = MakeSine(2, 440.0f, 0.5f, 1.0f);
        const AudioClipHandle clip = RegisterPcm(mixer, sine, 2);
        const AudioBusId bus = mixer.CreateBus(1.0f);
        AudioPlayDesc play;
        play.clip = clip;
        play.loop = true;
        play.bus = bus;
        mixer.Play(play);
        HalfGain half;
        mixer.SetBusProcessor(bus, &HalfGain::Process, &half);
        Check(std::fabs(Render(mixer, 9600).Peak(0, 4800) - 0.25f) < 0.03f && half.calls.load() > 0,
            "a bus processor changes the bus's sound");
        mixer.SetBusProcessor(bus, nullptr, nullptr);
        Check(std::fabs(Render(mixer, 9600).Peak(0, 4800) - 0.5f) < 0.05f, "removing it restores the sound");

        // 다른 스레드가 당기는 동안 걸고 떼기를 거듭한다. 뗀 뒤에 불리면 표지가 잡는다.
        std::atomic<bool> running{true};
        std::thread audio([&]
        {
            float buffer[480 * 2];
            while (running.load(std::memory_order_acquire))
            {
                mixer.Render(buffer, 480);
            }
        });
        bool calledAfterRemoval = false;
        int roundsThatRan = 0;
        for (int round = 0; round < 300; ++round)
        {
            SlowProbe local;
            mixer.SetBusProcessor(bus, &SlowProbe::Process, &local);
            // 처리기가 한 번은 불리기 시작할 때까지 기다린다 - 그래야 "부르는 중에 떼기" 가 자주 생긴다.
            const auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
            while (local.calls.load() == 0 && std::chrono::steady_clock::now() < limit)
            {
                std::this_thread::yield();
            }
            roundsThatRan += local.calls.load() > 0 ? 1 : 0;
            mixer.SetBusProcessor(bus, nullptr, nullptr);
            const int after = local.calls.load();
            calledAfterRemoval = calledAfterRemoval || local.active.load();
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            calledAfterRemoval = calledAfterRemoval || local.calls.load() != after;
        }
        std::cout << "  bus processor swapped while rendering: " << roundsThatRan << " of 300 rounds ran it" << '\n';
        Check(roundsThatRan > 200, "the processor ran in most rounds before it was removed");
        running.store(false, std::memory_order_release);
        audio.join();
        Check(false == calledAfterRemoval, "a processor is never called after SetBusProcessor returns");
        mixer.Shutdown();
    }

    // 버스 하나에 톤 하나를 걸어 두고 이펙트를 바꿔 가며 잰다(D-210).
    struct EffectBench
    {
        AudioMixer mixer;
        AudioBusId bus = AudioMasterBus;
        Array<float> sine;
        AudioVoiceHandle voice;

        void Open(float frequency, float amplitude)
        {
            Check(mixer.Initialize(SmallDesc()), "mixer initializes");
            bus = mixer.CreateBus(1.0f);
            sine = MakeSine(2, frequency, amplitude, 1.0f);
            AudioPlayDesc play;
            play.clip = RegisterPcm(mixer, sine, 2);
            play.loop = true;
            play.bus = bus;
            voice = mixer.Play(play);
        }

        // 효과가 자리 잡은 뒤(앞 0.1 초를 버림)의 소리다.
        Rendered Settle(const AudioBusEffects& effects, std::uint32_t frames = 9600)
        {
            mixer.SetBusEffects(bus, effects);
            Render(mixer, 4800);
            return Render(mixer, frames);
        }
    };

    float Rms(const Rendered& out)
    {
        double sum = 0.0;
        const std::size_t frames = out.samples.Size() / 2;
        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            sum += static_cast<double>(out.samples[frame * 2]) * out.samples[frame * 2];
        }
        return static_cast<float>(std::sqrt(sum / static_cast<double>(frames)));
    }

    void TestEqualizer()
    {
        EffectBench low;
        low.Open(60.0f, 0.1f);
        AudioBusEffects effects;
        effects.eqLowGain = 12.0f;
        const float boosted = low.Settle(effects).Peak(0);
        EffectBench high;
        high.Open(12000.0f, 0.4f);
        AudioBusEffects cut;
        cut.eqHighGain = -12.0f;
        const float trimmed = high.Settle(cut).Peak(0);
        EffectBench mid;
        mid.Open(1000.0f, 0.2f);
        AudioBusEffects peakEq;
        peakEq.eqMidGain = 6.0f;
        const float lifted = mid.Settle(peakEq).Peak(0);
        std::cout << "  EQ: 60 Hz under +12 dB low shelf 0.1 -> " << boosted << ", 12 kHz under -12 dB high shelf 0.4 -> "
                  << trimmed << ", 1 kHz under +6 dB mid 0.2 -> " << lifted << '\n';
        Check(boosted > 0.33f && boosted < 0.45f, "a +12 dB low shelf lifts a low tone about four times");
        Check(trimmed > 0.07f && trimmed < 0.13f, "a -12 dB high shelf cuts a high tone to about a quarter");
        Check(lifted > 0.36f && lifted < 0.44f, "a +6 dB mid peak doubles a tone at its frequency");
        AudioBusEffects flat;
        Check(std::fabs(mid.Settle(flat).Peak(0) - 0.2f) < 0.02f, "0 dB bands leave the tone alone");
    }

    void TestDistortionChorusAndPitch()
    {
        // 디스토션: 사인이 네모에 가까워진다 - 봉우리와 실효값의 비(사인은 1.41)가 1 쪽으로 준다.
        EffectBench drive;
        drive.Open(440.0f, 0.3f);
        AudioBusEffects driven;
        driven.distortion = 1.0f;
        const Rendered hot = drive.Settle(driven);
        const float crest = hot.Peak(0) / Rms(hot);
        std::cout << "  distortion crest factor: " << crest << " (a sine is 1.41)" << '\n';
        Check(crest < 1.15f, "full distortion squares the sine off");

        // 코러스: 흔들리는 지연과 섞여 크기가 시간에 따라 오르내린다. 끄면 고르다.
        EffectBench chorus;
        chorus.Open(440.0f, 0.4f);
        AudioBusEffects wide;
        wide.chorusMix = 1.0f;
        wide.chorusRate = 1.0f;
        wide.chorusDepth = 3.0f;
        chorus.mixer.SetBusEffects(chorus.bus, wide);
        float highest = 0.0f;
        float lowest = 1.0f;
        for (int window = 0; window < 15; ++window)
        {
            const float peak = Render(chorus.mixer, 4800).Peak(0);
            highest = std::fmax(highest, peak);
            lowest = std::fmin(lowest, peak);
        }
        std::cout << "  chorus: window peaks swing " << lowest << " .. " << highest << '\n';
        Check(highest - lowest > 0.05f, "a chorus makes the level swing over time");

        // 피치 시프트: 한 옥타브 올리면 영점 교차가 두 배, 내리면 절반이다(빠르기는 그대로).
        EffectBench pitch;
        pitch.Open(440.0f, 0.4f);
        AudioBusEffects up;
        up.pitchShift = 12.0f;
        const std::uint32_t upCrossings = pitch.Settle(up, Rate).ZeroCrossings(0);
        AudioBusEffects down;
        down.pitchShift = -12.0f;
        const std::uint32_t downCrossings = pitch.Settle(down, Rate).ZeroCrossings(0);
        std::cout << "  pitch shift of a 440 Hz tone: +12 -> " << upCrossings << " crossings/s, -12 -> " << downCrossings
                  << " (unshifted 880)" << '\n';
        Check(upCrossings > 1600 && upCrossings < 1950, "+12 semitones doubles the frequency");
        Check(downCrossings > 380 && downCrossings < 520, "-12 semitones halves it");
        Check(pitch.mixer.IsAlive(pitch.voice) && std::fabs(pitch.mixer.GetPlaybackSeconds(pitch.voice)) >= 0.0,
            "shifting pitch does not change how fast the voice plays");
    }

    void TestCompressorAndLimiter()
    {
        // 컴프레서: -6 dB 사인에 문턱 -20 dB·비율 4 → 넘친 14 dB 가 3.5 dB 가 된다(10.5 dB 줄임, 0.15 쯤).
        EffectBench comp;
        comp.Open(440.0f, 0.5f);
        AudioBusEffects squeeze;
        squeeze.compRatio = 4.0f;
        squeeze.compThreshold = -20.0f;
        const float squeezed = comp.Settle(squeeze).Peak(0, 4800);
        const float reduction = comp.mixer.GetBusGainReduction(comp.bus);
        std::cout << "  compressor: 0.5 -> " << squeezed << ", gain reduction " << reduction << " dB" << '\n';
        Check(squeezed > 0.12f && squeezed < 0.19f, "a 4:1 compressor pulls a loud tone down by its ratio");
        Check(reduction > 9.0f && reduction < 12.0f, "the bus reports how much it compressed");
        squeeze.compMakeup = 10.0f;
        Check(comp.Settle(squeeze).Peak(0, 4800) > squeezed * 2.5f, "makeup gain lifts the compressed bus back");
        Check(comp.Settle(AudioBusEffects{}).Peak(0, 4800) > 0.45f && comp.mixer.GetBusGainReduction(comp.bus) == 0.0f,
            "ratio 1 turns the compressor off");

        // 리미터: 같은 소리 둘이 겹쳐 1.6 이 되어도 천장 아래로 부드럽게 눌린다. 끄면 1 에서 잘린다.
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc()), "mixer initializes");
        const Array<float> loud = MakeSine(2, 440.0f, 0.8f, 1.0f);
        AudioPlayDesc play;
        play.clip = RegisterPcm(mixer, loud, 2);
        play.loop = true;
        mixer.Play(play);
        mixer.Play(play);
        const Rendered limited = Render(mixer, 9600);
        std::cout << "  limiter: two 0.8 tones -> peak " << limited.Peak(0, 4800) << ", before the limiter "
                  << mixer.GetStats().lastPeak << '\n';
        Check(mixer.IsOutputLimiterEnabled(), "the output limiter is on by default");
        Check(limited.Peak(0, 4800) <= 0.981f && limited.Peak(0, 4800) > 0.9f, "the limiter holds the sum under its ceiling");
        Check(mixer.GetStats().lastPeak > 1.5f, "the meter still shows how far the sum went over");
        mixer.SetOutputLimiter(false);
        Check(Render(mixer, 9600).Peak(0, 4800) == 1.0f, "with the limiter off the sum is clipped at 1");
        mixer.Shutdown();
    }

    // 새 칸을 모두 켜 둔 채 매 프레임 값을 바꿔도 할당이 없고, 다른 스레드가 당기는 동안 거듭 켜고 꺼도 샘플이 찢기지 않는다.
    void TestExtendedEffectsAreRealtimeSafe()
    {
        EffectBench bench;
        bench.Open(440.0f, 0.3f);
        AudioBusEffects all;
        all.eqLowGain = 3.0f;
        all.eqMidGain = -3.0f;
        all.eqHighGain = 2.0f;
        all.distortion = 0.3f;
        all.chorusMix = 0.5f;
        all.pitchShift = 5.0f;
        all.compRatio = 3.0f;
        bench.mixer.SetBusEffects(bench.bus, all);
        Array<float> buffer;
        buffer.Resize(960);
#if defined(_MSC_VER) && defined(_DEBUG)
        g_crtAllocations = 0;
        _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(&CountCrtAllocations);
#endif
        for (int frame = 0; frame < 200; ++frame)
        {
            all.eqMidHz = 500.0f + static_cast<float>(frame * 10);
            all.pitchShift = static_cast<float>((frame % 24) - 12);
            all.compThreshold = -30.0f + static_cast<float>(frame % 20);
            bench.mixer.SetBusEffects(bench.bus, all);
            bench.mixer.Render(buffer.Data(), 480);
        }
#if defined(_MSC_VER) && defined(_DEBUG)
        _CrtSetAllocHook(previous);
        Check(g_crtAllocations.load() == 0, "frames with every new effect on do not touch the heap");
#endif
        std::atomic<bool> running{true};
        std::atomic<bool> bad{false};
        std::thread audio([&]
        {
            float block[480 * 2];
            while (running.load(std::memory_order_acquire))
            {
                bench.mixer.Render(block, 480);
                for (float sample : block)
                {
                    if (!std::isfinite(sample))
                    {
                        bad.store(true, std::memory_order_relaxed);
                    }
                }
            }
        });
        for (int round = 0; round < 2000; ++round)
        {
            AudioBusEffects changing;
            changing.eqLowGain = (round % 3) == 0 ? 0.0f : 6.0f;
            changing.distortion = (round % 4) == 0 ? 0.0f : 0.5f;
            changing.chorusMix = (round % 5) == 0 ? 0.0f : 0.7f;
            changing.pitchShift = static_cast<float>((round % 25) - 12);
            changing.compRatio = (round % 2) == 0 ? 1.0f : 8.0f;
            bench.mixer.SetBusEffects(bench.bus, changing);
        }
        running.store(false, std::memory_order_release);
        audio.join();
        Check(false == bad.load(), "changing the new effects while rendering never produces a torn sample");
        bench.mixer.Shutdown();
    }

    void TestStealing()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc(4)), "mixer initializes");
        const Array<float> sine = MakeSine(2, 440.0f, 0.5f, 1.0f);
        const AudioClipHandle clip = RegisterPcm(mixer, sine, 2);
        AudioPlayDesc play;
        play.clip = clip;
        play.loop = true;
        AudioVoiceHandle voices[4];
        const float volumes[4] = {1.0f, 0.2f, 1.0f, 1.0f};
        for (int index = 0; index < 4; ++index)
        {
            play.volume = volumes[index];
            voices[index] = mixer.Play(play);
        }
        play.volume = 1.0f;
        AudioVoiceHandle fifth = mixer.Play(play);
        Check(fifth.IsSet(), "a full pool steals for an equal priority");
        Check(false == mixer.IsAlive(voices[1]), "the quietest voice of the lowest priority is stolen first");
        Check(mixer.IsAlive(voices[0]) && mixer.IsAlive(voices[2]) && mixer.IsAlive(voices[3]), "the others keep playing");

        AudioPlayDesc low = play;
        low.priority = 10;
        Check(false == mixer.Play(low).IsSet(), "a lower priority voice is rejected rather than stealing");
        Check(mixer.GetStats().voicesRejected == 1 && mixer.GetStats().voicesStolen == 1, "stats count steals and rejections");

        // 같은 우선순위·같은 크기면 가장 오래된 것이다.
        AudioVoiceHandle sixth = mixer.Play(play);
        Check(sixth.IsSet() && false == mixer.IsAlive(voices[0]), "with equal audibility the oldest voice is stolen");
        mixer.Shutdown();
    }

    void TestSteadyStateDoesNotAllocate()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc(8)), "mixer initializes");
        const Array<float> mono = MakeSine(1, 440.0f, 0.5f, 0.05f);
        const Array<float> stereo = MakeSine(2, 440.0f, 0.5f, 0.05f);
        const AudioClipHandle monoClip = RegisterPcm(mixer, mono, 1);
        const AudioClipHandle stereoClip = RegisterPcm(mixer, stereo, 2);
        const AudioBusId bus = mixer.CreateBus(1.0f);
        Array<float> buffer;
        buffer.Resize(960);
        const std::uint64_t growthsBefore = mixer.GetStats().allocatorGrowths;
#if defined(_MSC_VER) && defined(_DEBUG)
        g_crtAllocations = 0;
        _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(&CountCrtAllocations);
#endif
        for (int frame = 0; frame < 300; ++frame)
        {
            AudioPlayDesc play;
            play.clip = (frame % 2) == 0 ? monoClip : stereoClip;
            play.spatial = (frame % 3) == 0;
            play.bus = (frame % 5) == 0 ? bus : AudioMasterBus;
            play.pitch = (frame % 7) == 0 ? 1.5f : 1.0f;
            AudioVoiceHandle voice = mixer.Play(play);
            const float position[3] = {static_cast<float>(frame % 11), 0.0f, 0.0f};
            mixer.SetPosition(voice, position);
            mixer.Render(buffer.Data(), 480);
            if ((frame % 4) == 0)
            {
                mixer.Stop(voice);
            }
            mixer.Update();
        }
        mixer.StopAll();
#if defined(_MSC_VER) && defined(_DEBUG)
        _CrtSetAllocHook(previous);
        std::cout << "  CRT allocations during 300 frames: " << g_crtAllocations.load() << '\n';
        Check(g_crtAllocations.load() == 0, "a normal audio frame does not touch the CRT heap");
#endif
        const std::uint64_t growthsAfter = mixer.GetStats().allocatorGrowths;
        std::cout << "  fixed allocator growths after warm-up: " << (growthsAfter - growthsBefore) << '\n';
        Check(growthsAfter == growthsBefore, "the prewarmed fixed allocator serves every voice without growing");
        mixer.Shutdown();
    }

    // 오디오 스레드가 계속 당기는 동안 클립을 등록·재생·해제·해제된 메모리 반납을 거듭한다. 풀린 메모리를 읽으면
    // 디버그 힙이 채운 0xDD(-2e18 부근)가 섞여 나온다 - 그 값은 잘라도 ±1 이라 진폭 0.5 를 넘는다.
    void TestUnregisterWhileRendering()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc(8)), "mixer initializes");
        std::atomic<bool> running{true};
        std::atomic<bool> sawGarbage{false};
        std::atomic<std::uint64_t> renders{0};
        std::thread audio([&]
        {
            float buffer[480 * 2];
            while (running.load(std::memory_order_acquire))
            {
                mixer.Render(buffer, 480);
                for (float sample : buffer)
                {
                    if (std::fabs(sample) > 0.6f)
                    {
                        sawGarbage.store(true, std::memory_order_relaxed);
                    }
                }
                renders.fetch_add(1, std::memory_order_relaxed);
            }
        });

        double worstStallMicroseconds = 0.0;
        for (int round = 0; round < 200; ++round)
        {
            Array<float> sine = MakeSine(2, 330.0f, 0.5f, 0.2f);
            const AudioClipHandle clip = RegisterPcm(mixer, sine, 2);
            AudioPlayDesc play;
            play.clip = clip;
            play.loop = true;
            mixer.Play(play);
            const std::uint64_t seen = renders.load();
            while (renders.load() < seen + 2)
            {
                std::this_thread::yield();
            }
            const auto begin = std::chrono::steady_clock::now();
            mixer.UnregisterClip(clip);
            const auto end = std::chrono::steady_clock::now();
            const double stall = std::chrono::duration<double, std::micro>(end - begin).count();
            if (stall > worstStallMicroseconds)
            {
                worstStallMicroseconds = stall;
            }
            sine.Reset();
        }
        running.store(false, std::memory_order_release);
        audio.join();
        std::cout << "  worst UnregisterClip stall while rendering: " << worstStallMicroseconds << " us\n";
        Check(false == sawGarbage.load(), "the audio thread never reads a released clip");
        mixer.Shutdown();
    }

    void TestLifetimeRepeats()
    {
        const Array<float> sine = MakeSine(2, 440.0f, 0.5f, 0.5f);
        for (int round = 0; round < 20; ++round)
        {
            AudioMixer mixer;
            Check(mixer.Initialize(SmallDesc(4)), "mixer initializes repeatedly");
            const AudioClipHandle clip = RegisterPcm(mixer, sine, 2);
            AudioPlayDesc play;
            play.clip = clip;
            play.loop = true;
            mixer.Play(play);
            mixer.CreateBus(0.5f);
            float buffer[960];
            mixer.Render(buffer, 480);
            // 살아 있는 보이스·버스·클립을 둔 채로 내린다(기존 엔진 단계 2 의 반례).
        }
        AudioMixer twice;
        Check(twice.Initialize(SmallDesc()), "mixer initializes");
        Check(false == twice.Initialize(SmallDesc()), "a second Initialize is refused instead of leaking the engine");
        twice.Shutdown();
        twice.Shutdown();
        Check(false == twice.IsInitialized(), "shutdown is idempotent");
    }
}

int RunAudioMixerTests()
{
    const bool echo = JBro::Log::GetEchoToConsole();
    JBro::Log::SetEchoToConsole(false);
    try
    {
        TestSilenceWithoutVoices();
        TestVolumeAndBuses();
        TestPitchLoopAndEnd();
        TestDelayAndFade();
        TestSpatialization();
        TestEncodedClip();
        TestSeekWhilePlaying();
        TestBusEffects();
        TestEffectsChangeWhileRendering();
        TestBusRouting();
        TestVoiceFilter();
        TestOutputGainAndSpectrum();
        TestBusFadesDuckingAndTrim();
        TestBusProcessor();
        TestEqualizer();
        TestDistortionChorusAndPitch();
        TestCompressorAndLimiter();
        TestExtendedEffectsAreRealtimeSafe();
        TestStealing();
        TestSteadyStateDoesNotAllocate();
        TestUnregisterWhileRendering();
        TestLifetimeRepeats();
    }
    catch (const std::exception&)
    {
        JBro::Log::SetEchoToConsole(echo);
        return 1;
    }
    JBro::Log::SetEchoToConsole(echo);
    std::cout << "audio mixer tests passed\n";
    return 0;
}
