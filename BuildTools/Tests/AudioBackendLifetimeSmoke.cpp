#include "Core/Audio/IAudioBus.h"
#include "Core/Audio/IAudioEffect.h"
#include "Core/Audio/IAudioPlayer.h"
#include "Core/Audio/MiniAudio/MiniAudioDevice.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    template <typename T>
    void WriteBinary(std::ofstream& stream, const T& value)
    {
        stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    std::filesystem::path CreateSilentWaveFile()
    {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / L"jbro_audio_lifetime_smoke.wav";
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (false == stream.is_open())
        {
            return {};
        }

        constexpr std::uint16_t channels = 1;
        constexpr std::uint32_t sampleRate = 8000;
        constexpr std::uint16_t bitsPerSample = 16;
        constexpr std::uint32_t sampleCount = 800;
        constexpr std::uint16_t blockAlign = channels * bitsPerSample / 8;
        constexpr std::uint32_t byteRate = sampleRate * blockAlign;
        constexpr std::uint32_t dataSize = sampleCount * blockAlign;
        constexpr std::uint32_t riffSize = 36 + dataSize;
        constexpr std::uint16_t pcmFormat = 1;

        stream.write("RIFF", 4);
        WriteBinary(stream, riffSize);
        stream.write("WAVEfmt ", 8);
        constexpr std::uint32_t formatSize = 16;
        WriteBinary(stream, formatSize);
        WriteBinary(stream, pcmFormat);
        WriteBinary(stream, channels);
        WriteBinary(stream, sampleRate);
        WriteBinary(stream, byteRate);
        WriteBinary(stream, blockAlign);
        WriteBinary(stream, bitsPerSample);
        stream.write("data", 4);
        WriteBinary(stream, dataSize);
        const std::array<std::int16_t, sampleCount> silence{};
        stream.write(reinterpret_cast<const char*>(silence.data()), dataSize);
        return stream.good() ? path : std::filesystem::path{};
    }

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::wstring wide = path.wstring();
        const int byteCount = WideCharToMultiByte(
            CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (byteCount <= 0)
        {
            return {};
        }

        std::string utf8(static_cast<std::size_t>(byteCount), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), utf8.data(), byteCount, nullptr, nullptr);
        return utf8;
    }
}

int main()
{
    const std::filesystem::path wavePath = CreateSilentWaveFile();
    const std::string wavePathUtf8 = ToUtf8(wavePath);
    if (wavePath.empty() || wavePathUtf8.empty())
    {
        std::cerr << "Failed to create the lifetime smoke WAV file.\n";
        return 1;
    }

    for (int iteration = 0; iteration < 100; ++iteration)
    {
        CMiniAudioDevice device;
        AudioDeviceDesc desc;
        if (false == device.Initialize(desc))
        {
            std::cerr << "Audio device initialization failed at iteration " << iteration << ".\n";
            return 2;
        }
        if (device.Initialize(desc))
        {
            std::cerr << "Duplicate audio device initialization unexpectedly succeeded.\n";
            return 3;
        }

        OwnerPtr<IAudioBus> customBus = device.CreateBus("Ambience");
        OwnerPtr<IAudioEffect> effect = device.CreateEffect(EAudioEffectKind::Reverb);
        OwnerPtr<IAudioEffect> lingeringEffect = device.CreateEffect(EAudioEffectKind::Echo);
        OwnerPtr<IAudioPlayer> player = device.CreatePlayerFromFile(wavePathUtf8.c_str());
        player->SetEffectChain({
            effect.GetSafePtr(),
            effect.GetSafePtr(),
            lingeringEffect.GetSafePtr(),
        });
        player->Play();

        // The effect owner may disappear before the player. The player must be
        // detached from the effect node before that node is uninitialized.
        effect.Reset();
        player->SetVolume(0.5f);

        // All backend children intentionally outlive Finalize(). Calls and later
        // destruction must be harmless after the device has stopped its engine.
        device.Finalize();
        player->Stop();
        customBus->SetVolume(0.25f);
        lingeringEffect->SetParameter("wet", 0.25f);

        // Reinitializing the same device must create an independent backend while
        // owners from the previous backend are still alive but inert.
        if (false == device.Initialize(desc))
        {
            std::cerr << "Audio device reinitialization failed at iteration " << iteration << ".\n";
            return 4;
        }
        device.Finalize();

        player.Reset();
        lingeringEffect.Reset();
        customBus.Reset();
    }

    // The destructor is the final safety net when an explicit Finalize call is
    // omitted. Backend children are intentionally destroyed afterwards.
    OwnerPtr<IAudioPlayer> playerAfterDevice;
    OwnerPtr<IAudioEffect> effectAfterDevice;
    OwnerPtr<IAudioBus> busAfterDevice;
    {
        CMiniAudioDevice device;
        AudioDeviceDesc desc;
        if (false == device.Initialize(desc))
        {
            std::cerr << "Audio device initialization failed for destructor coverage.\n";
            return 5;
        }
        playerAfterDevice = device.CreatePlayerFromFile(wavePathUtf8.c_str());
        effectAfterDevice = device.CreateEffect(EAudioEffectKind::LowPass);
        busAfterDevice = device.CreateBus("Ambience");
        playerAfterDevice->AttachEffect(effectAfterDevice.GetSafePtr());
    }
    playerAfterDevice->Stop();
    effectAfterDevice->SetParameter("cutoff", 500.0f);
    busAfterDevice->SetMuted(true);
    playerAfterDevice.Reset();
    effectAfterDevice.Reset();
    busAfterDevice.Reset();

    // ── Bus routing ─────────────────────────────────────────────────
    // Buses are named by the project settings, so the device must build exactly the
    // list it was handed - plus Master, which is implicit. Two things are easy to
    // break and impossible to hear in a log: a configured name not producing a bus,
    // and CreatePlayer ignoring desc.Bus the way it used to.
    {
        CMiniAudioDevice device;
        AudioDeviceDesc desc;
        if (false == device.Initialize(desc))
        {
            std::cerr << "Audio device initialization failed for bus routing coverage.\n";
            return 6;
        }

        const std::vector<std::string> configured = { "Music", "SFX", "Ambience" };
        device.ConfigureBuses(configured);

        // Master is implicit and always first; the rest follow the configured order.
        const std::vector<std::string> expected = { AUDIO_MASTER_BUS_NAME, "Music", "SFX", "Ambience" };
        if (device.GetBusNames() != expected)
        {
            std::cerr << "Configured bus list did not round-trip through the device.\n";
            return 7;
        }

        for (const std::string& name : expected)
        {
            SafePtr<IAudioBus> bus = device.GetBus(name.c_str());
            if (false == bus.IsValid() || name != bus->GetName())
            {
                std::cerr << "Bus '" << name << "' is missing or mislabelled.\n";
                return 8;
            }
        }

        // An unknown name must not vanish - it falls back to Master so the sound is
        // still audible and the warning explains why it moved.
        SafePtr<IAudioBus> unknown = device.GetBus("NoSuchBus");
        if (false == unknown.IsValid() || 0 != std::strcmp(unknown->GetName(), AUDIO_MASTER_BUS_NAME))
        {
            std::cerr << "An unknown bus name did not fall back to Master.\n";
            return 9;
        }

        SafePtr<IAudioBus> musicBus = device.GetBus("Music");
        AudioPlayerDesc playerDesc;
        playerDesc.StreamPathUtf8 = wavePathUtf8.c_str();
        playerDesc.Bus = musicBus;
        OwnerPtr<IAudioPlayer> routed = device.CreatePlayer(playerDesc);
        if (false == bool(routed))
        {
            std::cerr << "CreatePlayer refused a bus-routed descriptor.\n";
            return 10;
        }
        // Category volume must reach the bus a player was routed into. This only
        // proves the call path survives; whether it is audible is a listening test.
        musicBus->SetVolume(0.5f);
        if (musicBus->GetVolume() != 0.5f)
        {
            std::cerr << "Bus volume did not stick.\n";
            return 11;
        }

        routed.Reset();
        device.Finalize();
    }

    std::error_code removeError;
    std::filesystem::remove(wavePath, removeError);
    std::cout << "100/100 audio backend lifetime iterations passed.\n";
    std::cout << "Audio bus routing checks passed.\n";
    return 0;
}
