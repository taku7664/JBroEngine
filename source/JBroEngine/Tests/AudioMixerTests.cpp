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
