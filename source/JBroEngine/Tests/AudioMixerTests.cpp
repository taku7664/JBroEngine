#include <JBro/Audio/AudioMixer.h>
#include <JBro/Core/Log.h>
#include <JBro/Types/Array.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <JBro/Types/UInt.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

// 오디오 1 단계(audio-plan §3-1): 장치 없이 믹서를 당겨 결과를 잰다. 소리는 나지 않는다.
namespace
{
    using namespace JBro;

    constexpr JBro::UInt32 Rate = 48000;
    constexpr JBro::Float Pi = 3.14159265358979f;

    void Check(JBro::Bool condition, const char* message)
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

    Array<float> MakeSine(JBro::UInt32 channels, JBro::Float frequency, JBro::Float amplitude, JBro::Float seconds)
    {
        const JBro::UInt32 frames = static_cast<std::uint32_t>(seconds * static_cast<float>(Rate));
        Array<float> samples;
        samples.Resize(static_cast<std::size_t>(frames) * channels);
        for (JBro::UInt32 frame = 0; frame < frames; ++frame)
        {
            const JBro::Float value = amplitude * std::sin(2.0f * Pi * frequency * static_cast<float>(frame) / static_cast<float>(Rate));
            for (JBro::UInt32 channel = 0; channel < channels; ++channel)
            {
                samples[static_cast<std::size_t>(frame) * channels + channel] = value;
            }
        }
        return samples;
    }

    AudioClipHandle RegisterPcm(AudioMixer& mixer, const Array<float>& samples, JBro::UInt32 channels)
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
        JBro::Float Peak(std::size_t channel, std::size_t beginFrame = 0, std::size_t endFrame = static_cast<std::size_t>(-1)) const
        {
            JBro::Float peak = 0.0f;
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

        JBro::UInt32 ZeroCrossings(std::size_t channel) const
        {
            JBro::UInt32 crossings = 0;
            const std::size_t frames = samples.Size() / 2;
            for (std::size_t frame = 1; frame < frames; ++frame)
            {
                const JBro::Float previous = samples[(frame - 1) * 2 + channel];
                const JBro::Float current = samples[frame * 2 + channel];
                if ((previous < 0.0f) != (current < 0.0f))
                {
                    ++crossings;
                }
            }
            return crossings;
        }
    };

    Rendered Render(AudioMixer& mixer, JBro::UInt32 frames)
    {
        Rendered result;
        result.samples.Resize(static_cast<std::size_t>(frames) * 2);
        // 장치의 콜백처럼 작은 조각으로 당긴다.
        JBro::UInt32 done = 0;
        while (done < frames)
        {
            const JBro::UInt32 chunk = frames - done < 480 ? frames - done : JBro::UInt32(480);
            mixer.Render(result.samples.Data() + static_cast<std::size_t>(done) * 2, chunk);
            done += chunk;
        }
        return result;
    }

    AudioMixerDesc SmallDesc(JBro::UInt32 voices = 8)
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
        const JBro::Float fullPeak = out.Peak(0);
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
        const JBro::UInt32 normalCrossings = out.ZeroCrossings(0);
        mixer.Stop(normal);

        play.pitch = 2.0f;
        AudioVoiceHandle doubled = mixer.Play(play);
        out = Render(mixer, 9600);
        const JBro::UInt32 doubledCrossings = out.ZeroCrossings(0);
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
        const JBro::Float nearPeak = out.Peak(1, 2400);

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
    Array<std::uint8_t> MakeWav(JBro::UInt32 frames, JBro::Float frequency, JBro::Float amplitude)
    {
        Array<std::uint8_t> bytes;
        const JBro::UInt32 dataBytes = frames * 2 * 2;
        bytes.Resize(44 + dataBytes);
        auto put32 = [&](std::size_t at, JBro::UInt32 value)
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
        for (JBro::UInt32 frame = 0; frame < frames; ++frame)
        {
            const JBro::Float value = amplitude * std::sin(2.0f * Pi * frequency * static_cast<float>(frame) / static_cast<float>(Rate));
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
        const JBro::Float dry = Render(mixer, 9600).Peak(0, 4800);
        AudioBusEffects effects;
        effects.lowPassHz = 300.0f;
        mixer.SetBusEffects(bus, effects);
        const JBro::Float cut = Render(mixer, 9600).Peak(0, 4800);
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
        const JBro::Float first = echoed.Peak(0, 0, 2400);
        const JBro::Float gap = echoed.Peak(0, 3500, 9000);
        const JBro::Float repeat = echoed.Peak(0, 9600, 12500);
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
        for (JBro::Int32 frame = 0; frame < 200; ++frame)
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
                for (JBro::Float sample : buffer)
                {
                    if (!std::isfinite(sample))
                    {
                        bad.store(true, std::memory_order_relaxed);
                    }
                }
            }
        });
        AudioBusEffects effects;
        for (JBro::Int32 round = 0; round < 2000; ++round)
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
        JBro::Float peak = Render(mixer, 9600).Peak(0, 4800);
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
        const JBro::Float returned = mixer.GetBusPeak(reverb);
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
        const JBro::Float both = Render(mixer, 9600).Peak(0, 4800);
        mixer.SetBusSolo(music, true);
        const JBro::Float soloed = Render(mixer, 9600).Peak(0, 4800);
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
        const JBro::Float cut = Render(mixer, 9600).Peak(0, 4800);
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
        for (JBro::Int32 frame = 0; frame < 200; ++frame)
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
        JBro::Float largestJump = 0.0f;
        for (std::size_t frame = 1; frame < fading.samples.Size() / 2; ++frame)
        {
            largestJump = std::fmax(largestJump, std::fabs(fading.samples[frame * 2] - fading.samples[(frame - 1) * 2]));
        }
        const JBro::Float halfway = fading.Peak(0, 2000, 2600);
        std::cout << "  output gain fade: halfway " << halfway << ", after " << fading.Peak(0, 5000) << '\n';
        Check(halfway > 0.1f && halfway < 0.45f, "the output gain is still on its way halfway through the fade");
        Check(fading.Peak(0, 5000) < 0.001f, "the output gain reaches zero after its fade");
        // 1 kHz 사인의 한 샘플 차는 최대 2πf/rate × 0.5 ≈ 0.065 다. 뚝 끊기면 0.5 가까이 뛴다.
        Check(largestJump < 0.08f, "fading the output never jumps");
        mixer.SetOutputGain(1.0f, 0.0f);
        Render(mixer, 4800);

        float bands[32];
        mixer.ComputeSpectrum(bands, 32);
        JBro::UInt32 loudest = 0;
        for (JBro::UInt32 band = 1; band < 32; ++band)
        {
            if (bands[band] > bands[loudest])
            {
                loudest = band;
            }
        }
        const JBro::Float from = 30.0f * std::pow(24000.0f / 30.0f, static_cast<float>(loudest) / 32.0f);
        const JBro::Float to = 30.0f * std::pow(24000.0f / 30.0f, static_cast<float>(loudest + 1) / 32.0f);
        std::cout << "  spectrum: loudest band " << loudest << " (" << from << ".." << to << " Hz) = " << bands[loudest] << '\n';
        Check(from <= 1050.0f && to >= 950.0f, "the loudest spectrum band holds the 1 kHz tone");
        // 0.5 는 -6 dB 이므로 (72 - 6) / 72 ≈ 0.92 다.
        Check(bands[loudest] > 0.85f && bands[loudest] < 0.97f, "the spectrum reads the tone's level in decibels");
        Check(bands[31] < bands[loudest] - 0.3f, "bands far from the tone are much quieter");
        float recent[64];
        Check(mixer.CopyRecentOutput(recent, 64) == 64, "the recent output can be copied");
        mixer.Shutdown();
    }

    JBro::Float LargestJump(const Rendered& out)
    {
        JBro::Float largest = 0.0f;
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
        const JBro::Float halfway = fading.Peak(0, 2000, 2800);
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
        const JBro::Float ducked = mixer.GetBusPeak(music);
        Render(mixer, Rate / 2);
        mixer.Update();
        const JBro::Float recovered = mixer.GetBusPeak(music);
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
        static void Process(void* user, float* frames, JBro::UInt32 frameCount, JBro::UInt32 channels, JBro::UInt32)
        {
            HalfGain* self = static_cast<HalfGain*>(user);
            self->calls.fetch_add(1, std::memory_order_relaxed);
            for (JBro::UInt32 index = 0; index < frameCount * channels; ++index)
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
        static void Process(void* user, float*, JBro::UInt32, JBro::UInt32, JBro::UInt32)
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
        JBro::Bool calledAfterRemoval = false;
        JBro::Int32 roundsThatRan = 0;
        for (JBro::Int32 round = 0; round < 300; ++round)
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
            const JBro::Int32 after = local.calls.load();
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

        void Open(JBro::Float frequency, JBro::Float amplitude)
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
        Rendered Settle(const AudioBusEffects& effects, JBro::UInt32 frames = 9600)
        {
            mixer.SetBusEffects(bus, effects);
            Render(mixer, 4800);
            return Render(mixer, frames);
        }
    };

    JBro::Float Rms(const Rendered& out)
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
        const JBro::Float boosted = low.Settle(effects).Peak(0);
        EffectBench high;
        high.Open(12000.0f, 0.4f);
        AudioBusEffects cut;
        cut.eqHighGain = -12.0f;
        const JBro::Float trimmed = high.Settle(cut).Peak(0);
        EffectBench mid;
        mid.Open(1000.0f, 0.2f);
        AudioBusEffects peakEq;
        peakEq.eqMidGain = 6.0f;
        const JBro::Float lifted = mid.Settle(peakEq).Peak(0);
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
        const JBro::Float crest = hot.Peak(0) / Rms(hot);
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
        JBro::Float highest = 0.0f;
        JBro::Float lowest = 1.0f;
        for (JBro::Int32 window = 0; window < 15; ++window)
        {
            const JBro::Float peak = Render(chorus.mixer, 4800).Peak(0);
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
        const JBro::UInt32 upCrossings = pitch.Settle(up, Rate).ZeroCrossings(0);
        AudioBusEffects down;
        down.pitchShift = -12.0f;
        const JBro::UInt32 downCrossings = pitch.Settle(down, Rate).ZeroCrossings(0);
        std::cout << "  pitch shift of a 440 Hz tone: +12 -> " << upCrossings << " crossings/s, -12 -> " << downCrossings
                  << " (unshifted 880)" << '\n';
        Check(upCrossings > 1600 && upCrossings < 1950, "+12 semitones doubles the frequency");
        Check(downCrossings > 380 && downCrossings < 520, "-12 semitones halves it");
        // 두 번 가라앉혀 (0.1 + 1) × 2 = 2.2 초를 섞었다 - 1 초 루프의 커서는 0.2 초여야 한다.
        const double cursor = pitch.mixer.GetPlaybackSeconds(pitch.voice);
        Check(pitch.mixer.IsAlive(pitch.voice) && std::fabs(cursor - 0.2) < 0.01,
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
        const JBro::Float squeezed = comp.Settle(squeeze).Peak(0, 4800);
        const JBro::Float reduction = comp.mixer.GetBusGainReduction(comp.bus);
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
        Check(limited.Peak(0, 4800) > 0.9f, "the limiter lets the sum reach its ceiling");
        Check(limited.Peak(0) <= 0.981f && limited.Peak(1) <= 0.981f, "the limiter holds the sum under its ceiling from the first sample");
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
        for (JBro::Int32 frame = 0; frame < 200; ++frame)
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
                for (JBro::Float sample : block)
                {
                    if (!std::isfinite(sample))
                    {
                        bad.store(true, std::memory_order_relaxed);
                    }
                }
            }
        });
        for (JBro::Int32 round = 0; round < 2000; ++round)
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
        for (JBro::Int32 index = 0; index < 4; ++index)
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

    // 클립의 동시 수와 쿨다운(D-231). 연타한 같은 소리가 보이스를 다 먹지 않는다.
    void TestInstanceLimitAndCooldown()
    {
        AudioMixer mixer;
        // 줄여 끄는 보이스가 한꺼번에 몰려도 훔치기가 끼어들지 않을 만큼 둔다.
        Check(mixer.Initialize(SmallDesc(40)), "mixer initializes");
        const Array<float> sine = MakeSine(2, 440.0f, 0.3f, 1.0f);
        AudioClipDesc desc;
        desc.encoding = AudioClipEncoding::Pcm;
        desc.pcm = sine.Data();
        desc.channels = 2;
        desc.sampleRate = Rate;
        desc.frameCount = sine.Size() / 2;
        desc.maxInstances = 2;
        const AudioClipHandle limited = mixer.RegisterClip(desc);
        AudioPlayDesc play;
        play.clip = limited;
        play.loop = true;
        AudioVoiceHandle first = mixer.Play(play);
        AudioVoiceHandle second = mixer.Play(play);
        AudioVoiceHandle third = mixer.Play(play);
        Check(first.IsSet() && second.IsSet() && third.IsSet(), "a full clip still plays the new instance");
        Check(mixer.GetStats().voicesReplaced == 1, "by replacing an old one");
        Render(mixer, 2400);
        mixer.Update();
        Check(false == mixer.IsAlive(first) && mixer.IsAlive(second) && mixer.IsAlive(third),
            "the oldest instance fades out and is gone after 20 ms");
        Check(mixer.GetStats().activeVoices == 2, "the clip never holds more than its limit");
        // 30 번 몰아 틀어도 울리는 것은 둘이다.
        for (JBro::Int32 burst = 0; burst < 30; ++burst)
        {
            mixer.Play(play);
        }
        Render(mixer, 2400);
        mixer.Update();
        Check(mixer.GetStats().activeVoices == 2, "a burst of thirty plays still sounds as two");
        const Rendered burstOut = Render(mixer, 4800);
        Check(burstOut.Peak(0) < 0.65f, "so the burst is not thirty times louder");

        // 우선순위가 높은 인스턴스는 바꾸지 않는다 - 새것을 버린다.
        mixer.StopAll();
        AudioPlayDesc important = play;
        important.priority = 200;
        mixer.Play(important);
        mixer.Play(important);
        const JBro::UInt64 throttled = mixer.GetStats().voicesThrottled;
        Check(false == mixer.Play(play).IsSet() && mixer.GetStats().voicesThrottled == throttled + 1,
            "a lower-priority play is dropped rather than replacing important instances");

        // 쿨다운: 0.1 초 안의 재생은 버린다. 시계는 믹서가 섞은 시간이다.
        mixer.StopAll();
        desc.maxInstances = 0;
        desc.cooldownSeconds = 0.1f;
        const AudioClipHandle cooled = mixer.RegisterClip(desc);
        play.clip = cooled;
        play.loop = false;
        Check(mixer.Play(play).IsSet(), "the first play after a quiet spell sounds");
        Check(false == mixer.Play(play).IsSet(), "a second play in the same instant is dropped");
        Render(mixer, Rate / 20);
        Check(false == mixer.Play(play).IsSet(), "and so is one 50 ms later");
        Render(mixer, Rate / 20 + 480);
        Check(mixer.Play(play).IsSet(), "after the cooldown it plays again");
        Check(mixer.GetStats().voicesThrottled == throttled + 3, "the dropped plays are counted");
        mixer.Shutdown();
    }

    // 시작 자리에서 들리지 않는 한 번짜리 소리는 보이스를 잡지 않는다(D-231).
    void TestInaudibleStartsAreCulled()
    {
        AudioMixer mixer;
        Check(mixer.Initialize(SmallDesc(2)), "mixer initializes");
        const Array<float> sine = MakeSine(1, 440.0f, 0.5f, 1.0f);
        const AudioClipHandle clip = RegisterPcm(mixer, sine, 1);
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        const float forward[3] = {0.0f, 0.0f, -1.0f};
        const float up[3] = {0.0f, 1.0f, 0.0f};
        mixer.SetListener(origin, forward, up);
        // 보이스 둘을 가장 낮은 우선순위로 채운다 - 걸러지지 않으면 하나를 훔쳤을 것이다.
        AudioPlayDesc filler;
        filler.clip = clip;
        filler.loop = true;
        filler.priority = 0;
        const AudioVoiceHandle a = mixer.Play(filler);
        const AudioVoiceHandle b = mixer.Play(filler);

        AudioPlayDesc far;
        far.clip = clip;
        far.spatial = true;
        far.attenuation = AudioAttenuation::Linear;
        far.minDistance = 1.0f;
        far.maxDistance = 20.0f;
        far.position[0] = 40.0f;
        Check(false == mixer.Play(far).IsSet(), "a linear one-shot past its maximum distance is not started");
        Check(mixer.IsAlive(a) && mixer.IsAlive(b) && mixer.GetStats().voicesStolen == 0, "and steals nothing");
        Check(mixer.GetStats().voicesCulled == 1, "it is counted as culled");

        // 역감쇠는 최대 거리(20 m) 밖에서도 최대 거리의 값(1/20 = -26 dB)이라 들린다 - "최대 거리 밖이면 버린다" 로 짜면 소리가 사라진다.
        AudioPlayDesc inverse = far;
        inverse.attenuation = AudioAttenuation::Inverse;
        Check(mixer.Play(inverse).IsSet(), "an inverse one-shot past its maximum distance keeps that distance's gain and plays");
        inverse.volume = 0.01f;
        Check(false == mixer.Play(inverse).IsSet(), "the same sound at 1% volume falls under -60 dB and is culled");
        // 지수 감쇠도 최대 거리의 값((20/1)^-1 = -26 dB)을 지킨다.
        AudioPlayDesc exponential = far;
        exponential.attenuation = AudioAttenuation::Exponential;
        Check(mixer.Play(exponential).IsSet(), "an exponential one-shot past its maximum distance keeps that distance's gain");
        exponential.volume = 0.01f;
        Check(false == mixer.Play(exponential).IsSet(), "and at 1% volume it is culled");
        AudioPlayDesc looping = far;
        looping.loop = true;
        Check(mixer.Play(looping).IsSet(), "a loop is never culled - it may come closer");
        AudioPlayDesc flat = far;
        flat.spatial = false;
        Check(mixer.Play(flat).IsSet(), "a non-spatial sound is never culled");
        AudioPlayDesc near = far;
        near.position[0] = 5.0f;
        Check(mixer.Play(near).IsSet(), "a one-shot inside its range plays");
        Check(mixer.GetStats().voicesCulled == 3, "only the inaudible starts were culled");

        // 걸러진 재생은 쿨다운 시계를 건드리지 않는다.
        AudioClipDesc cooledDesc;
        cooledDesc.encoding = AudioClipEncoding::Pcm;
        cooledDesc.pcm = sine.Data();
        cooledDesc.channels = 1;
        cooledDesc.sampleRate = Rate;
        cooledDesc.frameCount = sine.Size();
        cooledDesc.cooldownSeconds = 1.0f;
        far.clip = mixer.RegisterClip(cooledDesc);
        Check(false == mixer.Play(far).IsSet(), "a culled play of a cooled clip");
        near.clip = far.clip;
        Check(mixer.Play(near).IsSet(), "does not start the cooldown");
        mixer.Shutdown();
    }

    // 믹서와 같은 레이트의 PCM 은 보이스마다 리샘플하지 않는다(D-231 의 근거 실측). 시간은 적기만 한다 - 기계마다 다르다.
    void MeasureResampleCost()
    {
        const auto measure = [](JBro::UInt32 clipRate) {
            AudioMixer mixer;
            Check(mixer.Initialize(SmallDesc(32)), "mixer initializes");
            Array<float> samples;
            samples.Resize(static_cast<std::size_t>(clipRate) * 2);
            for (std::size_t index = 0; index < samples.Size(); ++index)
            {
                samples[index] = 0.01f * std::sin(static_cast<float>(index) * 0.01f);
            }
            AudioClipDesc desc;
            desc.encoding = AudioClipEncoding::Pcm;
            desc.pcm = samples.Data();
            desc.channels = 2;
            desc.sampleRate = clipRate;
            desc.frameCount = clipRate;
            AudioPlayDesc play;
            play.clip = mixer.RegisterClip(desc);
            play.loop = true;
            for (JBro::Int32 voice = 0; voice < 32; ++voice)
            {
                mixer.Play(play);
            }
            Render(mixer, 4800);
            const auto begin = std::chrono::steady_clock::now();
            Render(mixer, Rate * 4);
            const auto end = std::chrono::steady_clock::now();
            mixer.Shutdown();
            return std::chrono::duration<double, std::milli>(end - begin).count();
        };
        const double matched = measure(Rate);
        const double resampled = measure(44100);
        std::cout << "  32 voices for 4 s: clips at the mixer rate " << matched << " ms, at 44.1 kHz " << resampled << " ms\n";
    }

    // 가상 보이스(D-235): 섞는 자리보다 루프가 많으면 들리는 크기가 작은 것부터 멈추고 위치만 센다.
    struct VirtualBench
    {
        AudioMixer mixer;
        Array<float> sine;
        AudioClipHandle clip;
        AudioBusId buses[4] = {};
        AudioVoiceHandle loops[4];

        void Open(JBro::UInt32 audible)
        {
            AudioMixerDesc desc = SmallDesc(8);
            desc.maxAudibleVoices = audible;
            Check(mixer.Initialize(desc), "mixer initializes");
            const float origin[3] = {0.0f, 0.0f, 0.0f};
            const float forward[3] = {0.0f, 0.0f, -1.0f};
            const float up[3] = {0.0f, 1.0f, 0.0f};
            mixer.SetListener(origin, forward, up);
            sine = MakeSine(1, 440.0f, 0.5f, 1.0f);
            clip = RegisterPcm(mixer, sine, 1);
            for (JBro::Int32 index = 0; index < 4; ++index)
            {
                buses[index] = mixer.CreateBus(1.0f);
            }
        }

        AudioVoiceHandle PlayLoop(JBro::Int32 bus, JBro::Float x)
        {
            AudioPlayDesc play;
            play.clip = clip;
            play.loop = true;
            play.spatial = true;
            play.attenuation = AudioAttenuation::Linear;
            play.minDistance = 1.0f;
            play.maxDistance = 40.0f;
            play.position[0] = x;
            play.bus = buses[bus];
            return mixer.Play(play);
        }

        void Move(AudioVoiceHandle voice, JBro::Float x)
        {
            const float position[3] = {x, 0.0f, 0.0f};
            mixer.SetPosition(voice, position);
        }

        // 한 번 갱신하고 0.1 초를 섞은 뒤 버스마다 소리가 나는지 본다(페이드 20 ms 는 지나간다).
        void Step(JBro::UInt32 frames = 4800)
        {
            mixer.Update();
            Render(mixer, frames);
        }

        JBro::Bool Heard(JBro::Int32 bus) const
        {
            return mixer.GetBusPeak(buses[bus]) > 0.005f;
        }
    };

    double Wrapped(double seconds)
    {
        return std::fmod(seconds, 1.0);
    }

    JBro::Bool NearCursor(double actual, double expected)
    {
        double difference = std::fabs(Wrapped(actual) - Wrapped(expected));
        difference = difference > 0.5 ? 1.0 - difference : difference;
        return difference < 0.02;
    }

    void TestVirtualVoices()
    {
        VirtualBench bench;
        bench.Open(2);
        AudioMixer& mixer = bench.mixer;
        bench.loops[0] = bench.PlayLoop(0, 2.0f);
        bench.loops[1] = bench.PlayLoop(1, 10.0f);
        bench.loops[2] = bench.PlayLoop(2, 20.0f);
        Check(mixer.GetStats().virtualVoices == 1, "a loop started over the mixing limit starts virtual");
        bench.loops[3] = bench.PlayLoop(3, 30.0f);
        bench.Step();
        bench.Step();
        Check(bench.Heard(0) && bench.Heard(1), "the two nearest loops are mixed");
        Check(false == bench.Heard(2) && false == bench.Heard(3), "the far loops are silent");
        Check(mixer.GetStats().virtualVoices == 2 && mixer.GetStats().activeVoices == 4, "they stay alive as virtual voices");
        for (const AudioVoiceHandle& loop : bench.loops)
        {
            Check(mixer.IsAlive(loop), "a virtual voice keeps its handle");
        }

        // 가상인 동안도 위치가 흐른다.
        const double before = mixer.GetPlaybackSeconds(bench.loops[3]);
        bench.Step(Rate / 2);
        Check(NearCursor(mixer.GetPlaybackSeconds(bench.loops[3]), before + 0.5), "a virtual voice's cursor keeps moving");

        // 가까이 오면 센 자리에서 이어 울리고, 밀려난 것이 가상이 된다.
        bench.Move(bench.loops[3], 1.0f);
        const double counted = mixer.GetPlaybackSeconds(bench.loops[3]);
        const JBro::UInt64 realizedBefore = mixer.GetStats().voicesRealized;
        bench.Step();
        bench.Step();
        Check(mixer.GetStats().voicesRealized == realizedBefore + 1, "a loop that comes close is realized");
        Check(bench.Heard(3) && false == bench.Heard(1), "it is heard and the weaker loop gives way");
        std::cout << "  virtual voice resumed at " << mixer.GetPlaybackSeconds(bench.loops[3]) << " s, counted "
                  << counted << " s + 0.2 s\n";
        Check(NearCursor(mixer.GetPlaybackSeconds(bench.loops[3]), counted + 0.2), "it resumes where the count says, not from the start");

        // 막 바뀐 것은 0.25 초 동안 그대로다 - 경계에서 떨지 않는다.
        const JBro::UInt64 switches = mixer.GetStats().voicesVirtualized + mixer.GetStats().voicesRealized;
        bench.Move(bench.loops[3], 30.0f);
        bench.Move(bench.loops[1], 1.5f);
        bench.Step(480);
        Check(mixer.GetStats().voicesVirtualized + mixer.GetStats().voicesRealized == switches,
            "a voice that just switched holds for the dwell time");
        bench.Step(Rate / 4);
        bench.Step();
        Check(bench.Heard(1) && false == bench.Heard(3), "after the dwell the ranking applies again");

        // 한 번짜리가 오면 가장 약한 루프가 자리를 내준다.
        AudioPlayDesc shot;
        shot.clip = bench.clip;
        const JBro::UInt32 virtualBefore = mixer.GetStats().virtualVoices;
        const AudioVoiceHandle once = mixer.Play(shot);
        Check(once.IsSet() && mixer.GetStats().virtualVoices == virtualBefore + 1,
            "a one-shot over the mixing limit parks the weakest loop");

        // 가상인 채 멈추면 위치가 굳는다.
        mixer.Stop(once);
        bench.Step(Rate / 2);
        // 가장 먼 것은 순위 밖이라 가상인 채 남는다.
        const AudioVoiceHandle parked = bench.loops[3];
        const double beforePause = mixer.GetPlaybackSeconds(parked);
        mixer.Pause(parked);
        const double paused = mixer.GetPlaybackSeconds(parked);
        Check(std::fabs(paused - beforePause) < 1e-3, "pausing a virtual voice keeps its position");
        bench.Step(Rate / 4);
        Check(std::fabs(mixer.GetPlaybackSeconds(parked) - paused) < 1e-6, "a paused virtual voice does not move");
        mixer.Resume(parked);
        bench.Step(Rate / 4);
        Check(NearCursor(mixer.GetPlaybackSeconds(parked), paused + 0.25), "and moves again after resuming");
        // 피치를 바꾸면 그 뒤로만 빠르게 센다. 옮기면 곧바로 그 자리다.
        const double beforePitch = mixer.GetPlaybackSeconds(parked);
        mixer.SetPitch(parked, 2.0f);
        bench.Step(Rate / 4);
        Check(NearCursor(mixer.GetPlaybackSeconds(parked), beforePitch + 0.5), "a virtual voice counts at its new pitch from then on");
        mixer.SetPitch(parked, 1.0f);
        mixer.Seek(parked, 0.1);
        Check(std::fabs(mixer.GetPlaybackSeconds(parked) - 0.1) < 1e-3, "seeking a virtual voice moves its count");

        // 루프가 풀리면 실제로 돌아와 끝까지 울고 거둬진다.
        mixer.SetLooping(parked, false);
        bench.Step();
        Check(bench.Heard(3), "a virtual voice that stops looping is realized to finish");
        for (JBro::Int32 frame = 0; frame < 12; ++frame)
        {
            bench.Step();
        }
        Check(false == mixer.IsAlive(parked), "and it is collected at its end");
        mixer.Shutdown();

        // 자리가 남아도 -60 dB 밑의 루프는 섞지 않는다. 가상 보이스를 끄면(0) 아무것도 가상이 되지 않는다.
        VirtualBench quiet;
        quiet.Open(4);
        quiet.loops[0] = quiet.PlayLoop(0, 50.0f);
        quiet.Step();
        Check(quiet.mixer.GetStats().virtualVoices == 1, "an inaudible loop is virtual even with room to spare");
        quiet.mixer.Shutdown();
        VirtualBench off;
        off.Open(0);
        for (JBro::Int32 index = 0; index < 4; ++index)
        {
            off.loops[index] = off.PlayLoop(index, 50.0f);
        }
        off.Step();
        Check(off.mixer.GetStats().virtualVoices == 0, "with no mixing limit nothing is virtualized");
        off.mixer.Shutdown();
    }

    // 가상 보이스를 오가도 힙을 건드리지 않는다(D-235).
    void TestVirtualVoicesDoNotAllocate()
    {
        VirtualBench bench;
        bench.Open(2);
        for (JBro::Int32 index = 0; index < 4; ++index)
        {
            bench.loops[index] = bench.PlayLoop(index, 5.0f + 5.0f * static_cast<float>(index));
        }
        bench.Step();
        // 시험의 `Render` 는 결과 배열을 새로 잡으므로 여기서는 미리 잡은 칸에 섞는다.
        Array<float> buffer;
        buffer.Resize(960);
        const JBro::UInt64 growthsBefore = bench.mixer.GetStats().allocatorGrowths;
#if defined(_MSC_VER) && defined(_DEBUG)
        g_crtAllocations = 0;
        _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(&CountCrtAllocations);
#endif
        for (JBro::Int32 frame = 0; frame < 120; ++frame)
        {
            // 0.3 초마다 가까운 루프가 바뀐다 - 머무는 시간을 넘겨 가상과 실제가 오간다.
            const JBro::Int32 nearest = (frame / 18) % 4;
            for (JBro::Int32 index = 0; index < 4; ++index)
            {
                bench.Move(bench.loops[index], index == nearest ? 1.0f : 15.0f + static_cast<float>(index));
            }
            bench.mixer.Update();
            bench.mixer.Render(buffer.Data(), 400);
            bench.mixer.Render(buffer.Data(), 400);
        }
#if defined(_MSC_VER) && defined(_DEBUG)
        _CrtSetAllocHook(previous);
        std::cout << "  CRT allocations during 120 frames of virtual voice switching: " << g_crtAllocations.load() << '\n';
        Check(g_crtAllocations.load() == 0, "switching virtual voices does not touch the CRT heap");
#endif
        Check(bench.mixer.GetStats().voicesRealized >= 4, "the loop switched between virtual and real");
        Check(bench.mixer.GetStats().allocatorGrowths == growthsBefore, "nor grow the fixed allocator");
        bench.mixer.Shutdown();
    }

    // 오디오 전체 점검(D-240)에서 찾은 보이스의 반례들이다. 각 검사는 고치기 전의 코드에서 떨어진다.
    void TestVoiceAuditRegressions()
    {
        const Array<float> longTone = MakeSine(1, 440.0f, 0.3f, 1.0f);
        const Array<float> blip = MakeSine(1, 440.0f, 0.3f, 0.05f);
        {
            // 섞는 수가 찼고 새 소리를 받을 자리가 없으면 아무것도 죽이지 않고 거절한다.
            AudioMixerDesc desc = SmallDesc(4);
            desc.maxAudibleVoices = 2;
            AudioMixer mixer;
            Check(mixer.Initialize(desc), "mixer initializes");
            const AudioClipHandle clip = RegisterPcm(mixer, longTone, 1);
            AudioPlayDesc shot;
            shot.clip = clip;
            shot.priority = 200;
            mixer.Play(shot);
            mixer.Play(shot);
            AudioPlayDesc loop;
            loop.clip = clip;
            loop.loop = true;
            loop.priority = 100;
            const AudioVoiceHandle loopA = mixer.Play(loop);
            const AudioVoiceHandle loopB = mixer.Play(loop);
            Check(mixer.GetStats().virtualVoices == 2, "two loops wait as virtual voices");
            shot.priority = 128;
            const JBro::UInt64 stolen = mixer.GetStats().voicesStolen;
            Check(false == mixer.Play(shot).IsSet(), "a one-shot that outranks no mixing voice is refused");
            Check(mixer.IsAlive(loopA) && mixer.IsAlive(loopB) && mixer.GetStats().voicesStolen == stolen,
                "and nothing was stolen for it");

            // 동시 수로 바꿀 인스턴스가 있어도, 거절되면 그 인스턴스를 끄지 않는다.
            mixer.StopAll();
            AudioClipDesc limitedDesc;
            limitedDesc.encoding = AudioClipEncoding::Pcm;
            limitedDesc.pcm = longTone.Data();
            limitedDesc.channels = 1;
            limitedDesc.sampleRate = Rate;
            limitedDesc.frameCount = longTone.Size();
            limitedDesc.maxInstances = 1;
            const AudioClipHandle limited = mixer.RegisterClip(limitedDesc);
            shot.clip = clip;
            shot.priority = 255;
            mixer.Play(shot);
            mixer.Play(shot);
            AudioPlayDesc limitedLoop;
            limitedLoop.clip = limited;
            limitedLoop.loop = true;
            const AudioVoiceHandle instance = mixer.Play(limitedLoop);
            AudioPlayDesc limitedShot;
            limitedShot.clip = limited;
            Check(false == mixer.Play(limitedShot).IsSet(), "a play with no mixing room is refused");
            Check(mixer.IsAlive(instance) && false == mixer.IsPaused(instance) && mixer.GetStats().voicesReplaced == 0,
                "and the instance it would have replaced keeps playing");
            mixer.Shutdown();
        }
        {
            // 낮은 우선순위의 한 번짜리가 높은 우선순위의 루프(음악)를 가상으로 밀어내지 않는다.
            AudioMixerDesc desc = SmallDesc(4);
            desc.maxAudibleVoices = 1;
            AudioMixer mixer;
            Check(mixer.Initialize(desc), "mixer initializes");
            const AudioClipHandle clip = RegisterPcm(mixer, longTone, 1);
            AudioPlayDesc music;
            music.clip = clip;
            music.loop = true;
            music.priority = 255;
            mixer.Play(music);
            AudioPlayDesc shot;
            shot.clip = clip;
            shot.priority = 0;
            Check(false == mixer.Play(shot).IsSet() && mixer.GetStats().virtualVoices == 0,
                "a priority 0 one-shot does not park a priority 255 loop");
            mixer.Shutdown();
        }
        {
            AudioMixer mixer;
            Check(mixer.Initialize(SmallDesc()), "mixer initializes");
            // 끝났지만 아직 거두지 않은 한 번짜리를 멈추면 거둔다 - 다시 틀 때 처음부터 다시 울리지 않게.
            AudioPlayDesc once;
            once.clip = RegisterPcm(mixer, blip, 1);
            const AudioVoiceHandle ended = mixer.Play(once);
            Render(mixer, 4800);
            mixer.Pause(ended);
            Check(false == mixer.IsAlive(ended), "pausing a one-shot that already ended collects it");

            // 자리를 다시 쓰는 클립은 옛 클립의 쿨다운을 물려받지 않는다.
            AudioClipDesc cooled;
            cooled.encoding = AudioClipEncoding::Pcm;
            cooled.pcm = longTone.Data();
            cooled.channels = 1;
            cooled.sampleRate = Rate;
            cooled.frameCount = longTone.Size();
            cooled.cooldownSeconds = 1.0f;
            AudioPlayDesc first;
            first.clip = mixer.RegisterClip(cooled);
            Check(mixer.Play(first).IsSet(), "the cooled clip plays");
            mixer.UnregisterClip(first.clip);
            AudioPlayDesc second;
            second.clip = mixer.RegisterClip(cooled);
            Check(second.clip.index == first.clip.index, "the next clip reuses the slot");
            Check(mixer.Play(second).IsSet(), "a new clip in a reused slot is not throttled by the old clip's cooldown");
            mixer.Shutdown();
        }
        {
            // 시작 지연 중에 가상이 된 루프는 지연이 끝나는 때부터 센다.
            AudioMixerDesc desc = SmallDesc(4);
            desc.maxAudibleVoices = 1;
            AudioMixer mixer;
            Check(mixer.Initialize(desc), "mixer initializes");
            const AudioClipHandle clip = RegisterPcm(mixer, longTone, 1);
            AudioPlayDesc delayed;
            delayed.clip = clip;
            delayed.loop = true;
            delayed.volume = 0.2f;
            delayed.startDelaySeconds = 0.5f;
            const AudioVoiceHandle late = mixer.Play(delayed);
            AudioPlayDesc loud;
            loud.clip = clip;
            loud.loop = true;
            mixer.Play(loud);
            mixer.Update();
            Render(mixer, Rate * 3 / 10);
            Check(mixer.GetStats().virtualVoices == 1 && mixer.GetPlaybackSeconds(late) < 0.01,
                "a loop parked during its start delay has not advanced before the delay ends");
            Render(mixer, Rate * 4 / 10);
            Check(std::fabs(mixer.GetPlaybackSeconds(late) - 0.2) < 0.02, "and counts from the end of the delay");
            mixer.Shutdown();
        }
    }

    JBro::Bool FailToOpen(void*, const char*, AudioFileDecoder&)
    {
        return false;
    }

    // 열지 못한 디스크 스트림 루프는 거둬지고 스트림 자리를 돌려준다(D-240). 전에는 소리 없이 영원히 자리를 쥐었다.
    void TestFailedLoopingStreamIsCollected()
    {
        AudioMixerDesc desc = SmallDesc(4);
        desc.maxStreams = 1;
        desc.openStream = &FailToOpen;
        AudioMixer mixer;
        Check(mixer.Initialize(desc), "mixer initializes");
        AudioClipDesc file;
        file.encoding = AudioClipEncoding::File;
        file.path = "missing.wav";
        file.channels = 2;
        file.sampleRate = Rate;
        file.frameCount = Rate;
        AudioPlayDesc play;
        play.clip = mixer.RegisterClip(file);
        play.loop = true;
        const AudioVoiceHandle voice = mixer.Play(play);
        Check(voice.IsSet(), "the stream starts opening");
        const auto begin = std::chrono::steady_clock::now();
        while (mixer.IsAlive(voice) && std::chrono::steady_clock::now() - begin < std::chrono::seconds(2))
        {
            Render(mixer, 480);
            mixer.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Check(false == mixer.IsAlive(voice), "a looping stream whose file cannot open is collected");
        Check(mixer.WaitForStreamsIdle(), "and its stream slot comes back");
        Check(mixer.Play(play).IsSet(), "so the next stream can start");
        mixer.Shutdown();
    }

    // 버스 이펙트의 반례들(D-240).
    void TestEffectAuditRegressions()
    {
        {
            // 유한하지 않은 샘플 하나가 부모 버스의 필터·컴프레서에 박혀 그 뒤를 모두 먹통으로 만들지 않는다.
            AudioMixer mixer;
            Check(mixer.Initialize(SmallDesc()), "mixer initializes");
            AudioBusEffects master;
            master.lowPassHz = 8000.0f;
            master.compRatio = 4.0f;
            mixer.SetBusEffects(AudioMasterBus, master);
            const AudioBusId child = mixer.CreateBus(1.0f);
            struct Poison
            {
                static void Run(void*, float* frames, JBro::UInt32, JBro::UInt32, JBro::UInt32)
                {
                    frames[0] = std::numeric_limits<float>::infinity();
                    frames[1] = std::numeric_limits<float>::quiet_NaN();
                }
            };
            mixer.SetBusProcessor(child, &Poison::Run, nullptr);
            const Array<float> tone = MakeSine(2, 440.0f, 0.5f, 1.0f);
            AudioPlayDesc play;
            play.clip = RegisterPcm(mixer, tone, 2);
            play.loop = true;
            play.bus = child;
            mixer.Play(play);
            Render(mixer, 2400);
            mixer.SetBusProcessor(child, nullptr, nullptr);
            const Rendered after = Render(mixer, 9600);
            Check(after.Peak(0, 4800) > 0.1f, "a bus fed a non-finite sample recovers once the source is clean");
            mixer.Shutdown();
        }
        {
            // 끈 메아리를 다시 켜면 끈 동안 멈춰 있던 옛 소리가 다시 나지 않는다.
            AudioMixer mixer;
            Check(mixer.Initialize(SmallDesc()), "mixer initializes");
            const AudioBusId bus = mixer.CreateBus(1.0f);
            AudioBusEffects echo;
            echo.echoMix = 0.5f;
            echo.echoDelay = 0.5f;
            echo.echoFeedback = 0.5f;
            echo.reverbMix = 0.5f;
            mixer.SetBusEffects(bus, echo);
            const Array<float> tone = MakeSine(2, 440.0f, 0.5f, 1.0f);
            AudioPlayDesc play;
            play.clip = RegisterPcm(mixer, tone, 2);
            play.loop = true;
            play.bus = bus;
            const AudioVoiceHandle voice = mixer.Play(play);
            Render(mixer, Rate / 2);
            mixer.Stop(voice);
            mixer.SetBusEffects(bus, AudioBusEffects{});
            Render(mixer, 4800);
            mixer.SetBusEffects(bus, echo);
            const Rendered revived = Render(mixer, Rate / 2);
            std::cout << "  echo and reverb turned back on over silence: peak " << revived.Peak(0) << '\n';
            Check(revived.Peak(0) < 0.001f, "turning echo and reverb back on does not replay the old tail");
            mixer.Shutdown();
        }
        {
            AudioMixer mixer;
            Check(mixer.Initialize(SmallDesc()), "mixer initializes");
            // 제 소리를 받는 버스(조상)를 트리거로 두면 늘 제 소리에 눌린다 - 거절한다.
            const AudioBusId music = mixer.CreateBus(1.0f);
            mixer.SetBusDucking(music, AudioMasterBus, 0.5f, 0.3f);
            Check(mixer.GetBusDuckTrigger(music) == AudioNoBus, "a bus cannot duck under a bus its own sound reaches");

            // 버스 자리를 다시 쓰면 옛 버스의 로우패스가 남지 않는다.
            AudioBusEffects dull;
            dull.lowPassHz = 200.0f;
            mixer.SetBusEffects(music, dull);
            mixer.DestroyProjectBuses();
            const AudioBusId fresh = mixer.CreateBus(1.0f);
            const Array<float> bright = MakeSine(2, 5000.0f, 0.5f, 1.0f);
            AudioPlayDesc play;
            play.clip = RegisterPcm(mixer, bright, 2);
            play.loop = true;
            play.bus = fresh;
            mixer.Play(play);
            Render(mixer, 4800);
            Check(Render(mixer, 4800).Peak(0) > 0.45f, "a reused bus slot starts without the old bus's effects");

            // 페이드 도중에 되돌리면 남은 거리만큼의 속도로 간다(10 초 페이드의 1 초 뒤에 되돌리면 1 초 만에 끝나지 않는다).
            mixer.SetBusVolume(fresh, 0.0f, 10.0f);
            Render(mixer, Rate);
            mixer.SetBusVolume(fresh, 1.0f, 10.0f);
            Render(mixer, Rate / 2);
            const JBro::Float level = Render(mixer, 4800).Peak(0);
            std::cout << "  fade reversed after 1 s of a 10 s fade, 0.5 s later: " << level << " of 0.5\n";
            Check(level < 0.5f * 0.93f, "reversing a fade keeps the requested duration");
            mixer.Shutdown();
        }
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
        const JBro::UInt64 growthsBefore = mixer.GetStats().allocatorGrowths;
#if defined(_MSC_VER) && defined(_DEBUG)
        g_crtAllocations = 0;
        _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(&CountCrtAllocations);
#endif
        for (JBro::Int32 frame = 0; frame < 300; ++frame)
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
        const JBro::UInt64 growthsAfter = mixer.GetStats().allocatorGrowths;
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
                for (JBro::Float sample : buffer)
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
        for (JBro::Int32 round = 0; round < 200; ++round)
        {
            Array<float> sine = MakeSine(2, 330.0f, 0.5f, 0.2f);
            const AudioClipHandle clip = RegisterPcm(mixer, sine, 2);
            AudioPlayDesc play;
            play.clip = clip;
            play.loop = true;
            mixer.Play(play);
            const JBro::UInt64 seen = renders.load();
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
            // 돌아온 뒤에 읽으면 들리도록 표지값으로 덮고 푼다. 디버그 힙의 채움이나 같은 크기의 새 사인에 기대지 않는다.
            for (float& sample : sine)
            {
                sample = 0.9f;
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
        for (JBro::Int32 round = 0; round < 20; ++round)
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

JBro::Int32 RunAudioMixerTests()
{
    const JBro::Bool echo = JBro::Log::GetEchoToConsole();
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
        TestInstanceLimitAndCooldown();
        TestInaudibleStartsAreCulled();
        MeasureResampleCost();
        TestVirtualVoices();
        TestVirtualVoicesDoNotAllocate();
        TestVoiceAuditRegressions();
        TestFailedLoopingStreamIsCollected();
        TestEffectAuditRegressions();
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
