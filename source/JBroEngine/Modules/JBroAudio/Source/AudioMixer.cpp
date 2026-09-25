#include <JBro/Audio/AudioMixer.h>

#include <JBro/Core/Log.h>
#include <JBro/Types/Allocator.h>
#include <JBro/Types/Array.h>

#include <miniaudio.h>

#include <atomic>
#include <cmath>
#include <cstring>

namespace JBro
{
    namespace
    {
        // miniaudio 의 할당을 받는 고정 할당기다. 크기를 2 의 거듭제곱 칸으로 올려 칸마다 빈 목록을 둔다. 한 번 힙에서
        // 받은 블록은 믹서가 내려갈 때까지 돌려주지 않고 다시 쓴다 - 그래서 예열 뒤의 정상 프레임은 힙을 건드리지 않는다.
        //
        // 잠금이 있다. miniaudio 는 메인 스레드에서만 할당하지만(보이스 시작·끝), 디코더가 오디오 스레드에서 할당하는
        // 경로가 없다고 원문으로 확인하지 못했으므로 둘이 부딪혀도 망가지지 않게 한다. 경합이 없으면 원자 연산 하나다.
        class FixedAllocator
        {
        public:
            static constexpr std::size_t HeaderSize = 16;
            static constexpr std::uint32_t MinShift = 6;
            static constexpr std::uint32_t MaxShift = 24;
            static constexpr std::uint32_t ClassCount = MaxShift - MinShift + 1;
            static constexpr std::uint32_t DirectClass = 0xFFu;

            ~FixedAllocator()
            {
                ReleaseAll();
            }

            void* Allocate(std::size_t size)
            {
                const std::uint32_t sizeClass = ClassOf(size + HeaderSize);
                Lock();
                void* block = nullptr;
                if (sizeClass != DirectClass && m_free[sizeClass] != nullptr)
                {
                    block = m_free[sizeClass];
                    std::memcpy(&m_free[sizeClass], block, sizeof(void*));
                }
                Unlock();
                if (block == nullptr)
                {
                    const std::size_t bytes = sizeClass == DirectClass ? size + HeaderSize : BlockBytes(sizeClass);
                    block = HeapAllocator{}.Allocate(bytes, HeaderSize);
                    if (block == nullptr)
                    {
                        return nullptr;
                    }
                    Lock();
                    ++m_growths;
                    if (sizeClass != DirectClass)
                    {
                        m_owned.Add(block);
                    }
                    Unlock();
                }
                Header* header = static_cast<Header*>(block);
                header->sizeClass = sizeClass;
                header->bytes = sizeClass == DirectClass ? size + HeaderSize : BlockBytes(sizeClass);
                return static_cast<std::byte*>(block) + HeaderSize;
            }

            void Free(void* memory)
            {
                if (memory == nullptr)
                {
                    return;
                }
                void* block = static_cast<std::byte*>(memory) - HeaderSize;
                const Header* header = static_cast<const Header*>(block);
                if (header->sizeClass == DirectClass)
                {
                    HeapAllocator{}.Deallocate(block, header->bytes, HeaderSize);
                    return;
                }
                const std::uint32_t sizeClass = header->sizeClass;
                Lock();
                std::memcpy(block, &m_free[sizeClass], sizeof(void*));
                m_free[sizeClass] = block;
                Unlock();
            }

            void* Reallocate(void* memory, std::size_t size)
            {
                if (memory == nullptr)
                {
                    return Allocate(size);
                }
                const Header* header = reinterpret_cast<const Header*>(static_cast<std::byte*>(memory) - HeaderSize);
                const std::size_t usable = header->bytes - HeaderSize;
                if (size <= usable)
                {
                    return memory;
                }
                void* replacement = Allocate(size);
                if (replacement == nullptr)
                {
                    return nullptr;
                }
                std::memcpy(replacement, memory, usable);
                Free(memory);
                return replacement;
            }

            std::uint64_t GetGrowths() const
            {
                return m_growths;
            }

            void ReleaseAll()
            {
                for (void* block : m_owned)
                {
                    const Header* header = static_cast<const Header*>(block);
                    HeapAllocator{}.Deallocate(block, header->bytes, HeaderSize);
                }
                m_owned.Reset();
                for (void*& head : m_free)
                {
                    head = nullptr;
                }
            }

            void Reserve(std::size_t blocks)
            {
                m_owned.Reserve(blocks);
            }

            static void* OnMalloc(std::size_t size, void* user)
            {
                return static_cast<FixedAllocator*>(user)->Allocate(size);
            }

            static void* OnRealloc(void* memory, std::size_t size, void* user)
            {
                return static_cast<FixedAllocator*>(user)->Reallocate(memory, size);
            }

            static void OnFree(void* memory, void* user)
            {
                static_cast<FixedAllocator*>(user)->Free(memory);
            }

            ma_allocation_callbacks Callbacks()
            {
                ma_allocation_callbacks callbacks = {};
                callbacks.pUserData = this;
                callbacks.onMalloc = &OnMalloc;
                callbacks.onRealloc = &OnRealloc;
                callbacks.onFree = &OnFree;
                return callbacks;
            }

        private:
            struct Header
            {
                std::uint32_t sizeClass;
                std::uint32_t reserved;
                std::size_t bytes;
            };
            static_assert(sizeof(Header) <= HeaderSize);

            static std::uint32_t ClassOf(std::size_t bytes)
            {
                std::uint32_t shift = MinShift;
                while (shift <= MaxShift && (std::size_t{1} << shift) < bytes)
                {
                    ++shift;
                }
                return shift > MaxShift ? DirectClass : shift - MinShift;
            }

            static std::size_t BlockBytes(std::uint32_t sizeClass)
            {
                return std::size_t{1} << (sizeClass + MinShift);
            }

            void Lock()
            {
                while (m_lock.test_and_set(std::memory_order_acquire))
                {
                }
            }

            void Unlock()
            {
                m_lock.clear(std::memory_order_release);
            }

            std::atomic_flag m_lock = ATOMIC_FLAG_INIT;
            void* m_free[ClassCount] = {};
            Array<void*> m_owned;
            std::uint64_t m_growths = 0;
        };

        float Clamp01(float value)
        {
            if (!(value > 0.0f))
            {
                return 0.0f;
            }
            return value > 1.0f ? 1.0f : value;
        }

        float SafePositive(float value, float fallback)
        {
            return std::isfinite(value) && value > 0.0f ? value : fallback;
        }

        ma_attenuation_model ToMiniaudio(AudioAttenuation attenuation)
        {
            switch (attenuation)
            {
            case AudioAttenuation::None:
                return ma_attenuation_model_none;
            case AudioAttenuation::Linear:
                return ma_attenuation_model_linear;
            case AudioAttenuation::Exponential:
                return ma_attenuation_model_exponential;
            case AudioAttenuation::Inverse:
            default:
                return ma_attenuation_model_inverse;
            }
        }
    }

    struct AudioMixer::State
    {
        enum class VoiceState : std::uint8_t
        {
            Free,
            Playing,
            Paused,
            Stopping
        };

        struct Voice
        {
            ma_sound sound = {};
            ma_audio_buffer_ref buffer = {};
            ma_decoder decoder = {};
            bool usesDecoder = false;
            std::uint32_t generation = 1;
            VoiceState state = VoiceState::Free;
            AudioClipHandle clip;
            AudioBusId bus = AudioMasterBus;
            std::uint8_t priority = 0;
            bool looping = false;
            float volume = 1.0f;
            std::uint64_t startSerial = 0;
            std::uint32_t tag = 0;
        };

        struct Bus
        {
            ma_sound_group group = {};
            bool used = false;
            bool muted = false;
            float volume = 1.0f;
        };

        struct Clip
        {
            AudioClipDesc desc;
            std::uint32_t generation = 1;
            bool used = false;
        };

        AudioMixerDesc desc;
        bool initialized = false;
        FixedAllocator allocator;
        ma_allocation_callbacks callbacks = {};
        ma_engine engine = {};
        Bus buses[AudioMaxBuses];
        std::uint32_t busCount = 0;
        // 크기는 초기화 때 한 번 정하고 다시 늘리지 않는다 - 노드 그래프가 `ma_sound` 의 주소를 들고 있다.
        Array<Voice> voices;
        Array<std::uint32_t> freeVoices;
        Array<Clip> clips;
        Array<std::uint32_t> freeClips;
        std::uint64_t serial = 0;
        std::uint64_t voicesStarted = 0;
        std::uint64_t voicesStolen = 0;
        std::uint64_t voicesRejected = 0;
        std::atomic<float> peak{0.0f};
        float masterVolume = 1.0f;

        Voice* Resolve(AudioVoiceHandle handle)
        {
            if (false == initialized || handle.index >= voices.Size())
            {
                return nullptr;
            }
            Voice& voice = voices[handle.index];
            if (voice.generation != handle.generation || voice.state == VoiceState::Free)
            {
                return nullptr;
            }
            return &voice;
        }

        const Voice* Resolve(AudioVoiceHandle handle) const
        {
            return const_cast<State*>(this)->Resolve(handle);
        }

        const Clip* ResolveClip(AudioClipHandle handle) const
        {
            if (false == initialized || handle.index >= clips.Size())
            {
                return nullptr;
            }
            const Clip& clip = clips[handle.index];
            return clip.used && clip.generation == handle.generation ? &clip : nullptr;
        }

        bool IsBusValid(AudioBusId bus) const
        {
            return bus < AudioMaxBuses && buses[bus].used;
        }

        // 보이스를 곧바로 내리고 자리를 돌려준다. `ma_sound_uninit` 은 오디오 스레드가 이 노드를 다 읽을 때까지
        // 기다린 뒤 돌아온다(miniaudio 7.2 절) - 그래서 돌아온 뒤에는 클립 메모리를 풀어도 된다.
        void ReleaseVoice(std::uint32_t index)
        {
            Voice& voice = voices[index];
            if (voice.state == VoiceState::Free)
            {
                return;
            }
            ma_sound_uninit(&voice.sound);
            if (voice.usesDecoder)
            {
                ma_decoder_uninit(&voice.decoder);
                voice.usesDecoder = false;
            }
            voice.state = VoiceState::Free;
            voice.clip = {};
            ++voice.generation;
            if (voice.generation == 0)
            {
                voice.generation = 1;
            }
            freeVoices.Add(index);
        }

        float Audibility(const Voice& voice) const
        {
            const Bus& bus = buses[voice.bus];
            return bus.muted ? 0.0f : voice.volume * bus.volume;
        }

        // 훔칠 보이스다. 우선순위가 가장 낮은 것, 같으면 작게 들리는 것, 같으면 가장 오래된 것이다. 새 보이스보다
        // 우선순위가 높은 것만 남았으면 훔치지 않는다. 결정적이다 - 같은 상태에서 늘 같은 것을 고른다.
        std::uint32_t PickVictim(std::uint8_t incomingPriority) const
        {
            std::uint32_t best = static_cast<std::uint32_t>(-1);
            for (std::uint32_t index = 0; index < voices.Size(); ++index)
            {
                const Voice& voice = voices[index];
                if (voice.state == VoiceState::Free || voice.priority > incomingPriority)
                {
                    continue;
                }
                if (best == static_cast<std::uint32_t>(-1))
                {
                    best = index;
                    continue;
                }
                const Voice& current = voices[best];
                if (voice.priority != current.priority)
                {
                    if (voice.priority < current.priority)
                    {
                        best = index;
                    }
                    continue;
                }
                const float audibility = Audibility(voice);
                const float currentAudibility = Audibility(current);
                if (audibility != currentAudibility)
                {
                    if (audibility < currentAudibility)
                    {
                        best = index;
                    }
                    continue;
                }
                if (voice.startSerial < current.startSerial)
                {
                    best = index;
                }
            }
            return best;
        }

        bool InitBus(Bus& bus, ma_sound_group* parent, float volume)
        {
            if (ma_sound_group_init(&engine, 0, parent, &bus.group) != MA_SUCCESS)
            {
                return false;
            }
            bus.used = true;
            bus.muted = false;
            bus.volume = Clamp01(volume);
            ma_sound_group_set_volume(&bus.group, bus.volume);
            return true;
        }

        void UninitBus(Bus& bus)
        {
            if (bus.used)
            {
                ma_sound_group_uninit(&bus.group);
                bus.used = false;
            }
        }

        // 보이스 수만큼 소리를 한꺼번에 만들었다 지워 고정 할당기의 빈 목록을 채운다. miniaudio 의 노드 힙 크기는
        // 채널 수와 공간화 여부에 따라 달라지므로 넷을 차례로 돈다.
        void Prewarm()
        {
            static const float silence[2] = {0.0f, 0.0f};
            Array<ma_audio_buffer_ref> buffers;
            Array<ma_sound> sounds;
            buffers.Resize(desc.maxVoices);
            sounds.Resize(desc.maxVoices);
            for (std::uint32_t channels = 1; channels <= 2; ++channels)
            {
                for (int spatial = 0; spatial < 2; ++spatial)
                {
                    std::uint32_t made = 0;
                    for (; made < desc.maxVoices; ++made)
                    {
                        if (ma_audio_buffer_ref_init(ma_format_f32, channels, silence, 1, &buffers[made]) != MA_SUCCESS)
                        {
                            break;
                        }
                        ma_sound_config config = ma_sound_config_init_2(&engine);
                        config.pDataSource = &buffers[made];
                        config.pInitialAttachment = reinterpret_cast<ma_node*>(&buses[AudioMasterBus].group);
                        config.flags = spatial != 0 ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
                        if (ma_sound_init_ex(&engine, &config, &sounds[made]) != MA_SUCCESS)
                        {
                            break;
                        }
                    }
                    for (std::uint32_t index = 0; index < made; ++index)
                    {
                        ma_sound_uninit(&sounds[index]);
                    }
                }
            }
        }
    };

    AudioMixer::AudioMixer() = default;

    AudioMixer::~AudioMixer()
    {
        Shutdown();
    }

    bool AudioMixer::Initialize(const AudioMixerDesc& desc)
    {
        if (m_state && m_state->initialized)
        {
            return false;
        }
        if (desc.sampleRate == 0 || desc.channels == 0 || desc.channels > 2 || desc.maxVoices == 0)
        {
            Log::Write(LogLevel::Error, "audio", "mixer: bad format %u Hz, %u channels, %u voices",
                desc.sampleRate, desc.channels, desc.maxVoices);
            return false;
        }
        m_state = MakeOwnerPtr<State>();
        State& state = *m_state;
        state.desc = desc;
        state.callbacks = state.allocator.Callbacks();

        ma_engine_config config = ma_engine_config_init();
        config.noDevice = MA_TRUE;
        config.channels = desc.channels;
        config.sampleRate = desc.sampleRate;
        config.listenerCount = 1;
        config.allocationCallbacks = state.callbacks;
        if (ma_engine_init(&config, &state.engine) != MA_SUCCESS)
        {
            Log::Write(LogLevel::Error, "audio", "mixer: ma_engine_init failed");
            m_state = nullptr;
            return false;
        }
        if (false == state.InitBus(state.buses[AudioMasterBus], nullptr, 1.0f)
            || false == state.InitBus(state.buses[AudioEditorPreviewBus], nullptr, 1.0f))
        {
            Log::Write(LogLevel::Error, "audio", "mixer: could not create the master bus");
            state.UninitBus(state.buses[AudioEditorPreviewBus]);
            state.UninitBus(state.buses[AudioMasterBus]);
            ma_engine_uninit(&state.engine);
            m_state = nullptr;
            return false;
        }
        state.busCount = AudioFirstProjectBus;

        state.voices.Resize(desc.maxVoices);
        state.freeVoices.Reserve(desc.maxVoices);
        for (std::uint32_t index = desc.maxVoices; index > 0; --index)
        {
            state.freeVoices.Add(index - 1);
        }
        state.clips.Reserve(desc.maxClips);
        state.freeClips.Reserve(desc.maxClips);
        state.allocator.Reserve(static_cast<std::size_t>(desc.maxVoices) * 16u + 64u);
        state.initialized = true;
        state.Prewarm();
        return true;
    }

    void AudioMixer::Shutdown()
    {
        if (!m_state)
        {
            return;
        }
        State& state = *m_state;
        if (state.initialized)
        {
            for (std::uint32_t index = 0; index < state.voices.Size(); ++index)
            {
                state.ReleaseVoice(index);
            }
            for (std::uint32_t bus = AudioMaxBuses; bus > AudioFirstProjectBus; --bus)
            {
                state.UninitBus(state.buses[bus - 1]);
            }
            state.UninitBus(state.buses[AudioEditorPreviewBus]);
            state.UninitBus(state.buses[AudioMasterBus]);
            ma_engine_uninit(&state.engine);
            state.initialized = false;
        }
        m_state = nullptr;
    }

    bool AudioMixer::IsInitialized() const
    {
        return m_state && m_state->initialized;
    }

    std::uint32_t AudioMixer::GetSampleRate() const
    {
        return IsInitialized() ? m_state->desc.sampleRate : 0;
    }

    std::uint32_t AudioMixer::GetChannels() const
    {
        return IsInitialized() ? m_state->desc.channels : 0;
    }

    void AudioMixer::Render(float* output, std::uint32_t frameCount)
    {
        if (output == nullptr || frameCount == 0)
        {
            return;
        }
        State* state = m_state.Get();
        if (state == nullptr || false == state->initialized)
        {
            const std::uint32_t channels = state != nullptr ? state->desc.channels : 2;
            std::memset(output, 0, sizeof(float) * frameCount * channels);
            return;
        }
        ma_uint64 read = 0;
        if (ma_engine_read_pcm_frames(&state->engine, output, frameCount, &read) != MA_SUCCESS)
        {
            read = 0;
        }
        const std::size_t samples = static_cast<std::size_t>(frameCount) * state->desc.channels;
        const std::size_t filled = static_cast<std::size_t>(read) * state->desc.channels;
        if (filled < samples)
        {
            std::memset(output + filled, 0, sizeof(float) * (samples - filled));
        }
        // 버스를 더하면 1 을 넘을 수 있다. 장치에 넘기 전에 자른다 - 넘친 값은 장치마다 다르게 깨진다.
        float peak = 0.0f;
        for (std::size_t index = 0; index < samples; ++index)
        {
            float sample = output[index];
            if (!std::isfinite(sample))
            {
                sample = 0.0f;
            }
            const float magnitude = std::fabs(sample);
            if (magnitude > peak)
            {
                peak = magnitude;
            }
            if (sample > 1.0f)
            {
                sample = 1.0f;
            }
            else if (sample < -1.0f)
            {
                sample = -1.0f;
            }
            output[index] = sample;
        }
        state->peak.store(peak, std::memory_order_relaxed);
    }

    void AudioMixer::RenderCallback(void* user, float* output, std::uint32_t frameCount)
    {
        static_cast<AudioMixer*>(user)->Render(output, frameCount);
    }

    void AudioMixer::Update()
    {
        if (false == IsInitialized())
        {
            return;
        }
        State& state = *m_state;
        for (std::uint32_t index = 0; index < state.voices.Size(); ++index)
        {
            State::Voice& voice = state.voices[index];
            if (voice.state == State::VoiceState::Playing)
            {
                if (false == voice.looping && ma_sound_at_end(&voice.sound))
                {
                    state.ReleaseVoice(index);
                }
            }
            else if (voice.state == State::VoiceState::Stopping)
            {
                if (false == ma_sound_is_playing(&voice.sound))
                {
                    state.ReleaseVoice(index);
                }
            }
        }
    }

    AudioClipHandle AudioMixer::RegisterClip(const AudioClipDesc& desc)
    {
        if (false == IsInitialized())
        {
            return {};
        }
        const bool pcmValid = desc.encoding == AudioClipEncoding::Pcm && desc.pcm != nullptr && desc.frameCount > 0
            && desc.channels > 0 && desc.channels <= 8 && desc.sampleRate > 0;
        const bool encodedValid = desc.encoding == AudioClipEncoding::Encoded && desc.bytes != nullptr && desc.byteCount > 0;
        if (false == pcmValid && false == encodedValid)
        {
            return {};
        }
        State& state = *m_state;
        std::uint32_t index = 0;
        if (false == state.freeClips.IsEmpty())
        {
            index = state.freeClips.Last();
            state.freeClips.RemoveAt(state.freeClips.Size() - 1);
        }
        else
        {
            if (state.clips.Size() >= state.desc.maxClips)
            {
                Log::Write(LogLevel::Warning, "audio", "mixer: clip limit %u reached", state.desc.maxClips);
                return {};
            }
            index = static_cast<std::uint32_t>(state.clips.Size());
            state.clips.Emplace();
        }
        State::Clip& clip = state.clips[index];
        clip.desc = desc;
        clip.used = true;
        return {index, clip.generation};
    }

    void AudioMixer::UnregisterClip(AudioClipHandle handle)
    {
        if (false == IsInitialized() || nullptr == m_state->ResolveClip(handle))
        {
            return;
        }
        State& state = *m_state;
        for (std::uint32_t index = 0; index < state.voices.Size(); ++index)
        {
            const State::Voice& voice = state.voices[index];
            if (voice.state != State::VoiceState::Free && voice.clip.index == handle.index
                && voice.clip.generation == handle.generation)
            {
                state.ReleaseVoice(index);
            }
        }
        State::Clip& clip = state.clips[handle.index];
        clip.used = false;
        clip.desc = {};
        ++clip.generation;
        if (clip.generation == 0)
        {
            clip.generation = 1;
        }
        state.freeClips.Add(handle.index);
    }

    bool AudioMixer::IsClipRegistered(AudioClipHandle clip) const
    {
        return IsInitialized() && m_state->ResolveClip(clip) != nullptr;
    }

    double AudioMixer::GetClipDurationSeconds(AudioClipHandle handle) const
    {
        if (false == IsInitialized())
        {
            return 0.0;
        }
        const State::Clip* clip = m_state->ResolveClip(handle);
        if (clip == nullptr || clip->desc.sampleRate == 0)
        {
            return 0.0;
        }
        return static_cast<double>(clip->desc.frameCount) / static_cast<double>(clip->desc.sampleRate);
    }

    AudioBusId AudioMixer::CreateBus(float volume)
    {
        if (false == IsInitialized())
        {
            return AudioMasterBus;
        }
        State& state = *m_state;
        for (std::uint32_t bus = AudioFirstProjectBus; bus < AudioMaxBuses; ++bus)
        {
            if (false == state.buses[bus].used)
            {
                if (false == state.InitBus(state.buses[bus], &state.buses[AudioMasterBus].group, volume))
                {
                    return AudioMasterBus;
                }
                if (bus + 1 > state.busCount)
                {
                    state.busCount = bus + 1;
                }
                return static_cast<AudioBusId>(bus);
            }
        }
        Log::Write(LogLevel::Warning, "audio", "mixer: bus limit %u reached", AudioMaxBuses - AudioFirstProjectBus);
        return AudioMasterBus;
    }

    void AudioMixer::DestroyProjectBuses()
    {
        if (false == IsInitialized())
        {
            return;
        }
        State& state = *m_state;
        for (State::Voice& voice : state.voices)
        {
            if (voice.state != State::VoiceState::Free && voice.bus >= AudioFirstProjectBus)
            {
                ma_node_attach_output_bus(&voice.sound, 0, &state.buses[AudioMasterBus].group, 0);
                voice.bus = AudioMasterBus;
            }
        }
        for (std::uint32_t bus = AudioMaxBuses; bus > AudioFirstProjectBus; --bus)
        {
            state.UninitBus(state.buses[bus - 1]);
        }
        state.busCount = AudioFirstProjectBus;
    }

    std::uint32_t AudioMixer::GetBusCount() const
    {
        return IsInitialized() ? m_state->busCount : 0;
    }

    void AudioMixer::SetBusVolume(AudioBusId bus, float volume)
    {
        if (false == IsInitialized() || false == m_state->IsBusValid(bus))
        {
            return;
        }
        State::Bus& target = m_state->buses[bus];
        target.volume = Clamp01(volume);
        ma_sound_group_set_volume(&target.group, target.muted ? 0.0f : target.volume);
    }

    float AudioMixer::GetBusVolume(AudioBusId bus) const
    {
        if (false == IsInitialized() || false == m_state->IsBusValid(bus))
        {
            return 0.0f;
        }
        return m_state->buses[bus].volume;
    }

    void AudioMixer::SetBusMuted(AudioBusId bus, bool muted)
    {
        if (false == IsInitialized() || false == m_state->IsBusValid(bus))
        {
            return;
        }
        State::Bus& target = m_state->buses[bus];
        target.muted = muted;
        ma_sound_group_set_volume(&target.group, target.muted ? 0.0f : target.volume);
    }

    bool AudioMixer::IsBusMuted(AudioBusId bus) const
    {
        return IsInitialized() && m_state->IsBusValid(bus) && m_state->buses[bus].muted;
    }

    AudioVoiceHandle AudioMixer::Play(const AudioPlayDesc& desc)
    {
        if (false == IsInitialized())
        {
            return {};
        }
        State& state = *m_state;
        const State::Clip* clip = state.ResolveClip(desc.clip);
        if (clip == nullptr)
        {
            return {};
        }
        const AudioBusId bus = state.IsBusValid(desc.bus) ? desc.bus : AudioMasterBus;

        std::uint32_t index = 0;
        if (false == state.freeVoices.IsEmpty())
        {
            index = state.freeVoices.Last();
            state.freeVoices.RemoveAt(state.freeVoices.Size() - 1);
        }
        else
        {
            const std::uint32_t victim = state.PickVictim(desc.priority);
            if (victim == static_cast<std::uint32_t>(-1))
            {
                ++state.voicesRejected;
                return {};
            }
            state.ReleaseVoice(victim);
            ++state.voicesStolen;
            index = state.freeVoices.Last();
            state.freeVoices.RemoveAt(state.freeVoices.Size() - 1);
        }

        State::Voice& voice = state.voices[index];
        ma_data_source* source = nullptr;
        if (clip->desc.encoding == AudioClipEncoding::Pcm)
        {
            if (ma_audio_buffer_ref_init(ma_format_f32, clip->desc.channels, clip->desc.pcm, clip->desc.frameCount,
                    &voice.buffer) != MA_SUCCESS)
            {
                state.freeVoices.Add(index);
                return {};
            }
            voice.buffer.sampleRate = clip->desc.sampleRate;
            source = &voice.buffer;
        }
        else
        {
            ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
            config.allocationCallbacks = state.callbacks;
            if (ma_decoder_init_memory(clip->desc.bytes, clip->desc.byteCount, &config, &voice.decoder) != MA_SUCCESS)
            {
                state.freeVoices.Add(index);
                return {};
            }
            voice.usesDecoder = true;
            source = &voice.decoder;
        }

        ma_sound_config config = ma_sound_config_init_2(&state.engine);
        config.pDataSource = source;
        config.pInitialAttachment = reinterpret_cast<ma_node*>(&state.buses[bus].group);
        config.flags = desc.spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
        if (ma_sound_init_ex(&state.engine, &config, &voice.sound) != MA_SUCCESS)
        {
            if (voice.usesDecoder)
            {
                ma_decoder_uninit(&voice.decoder);
                voice.usesDecoder = false;
            }
            state.freeVoices.Add(index);
            return {};
        }

        // 여기서부터 `ma_sound_start` 전까지는 소리가 멈춰 있어 오디오 스레드가 읽지 않는다. 원자가 아닌 값(거리·
        // 감쇠·도플러)은 이 사이에만 쓴다(D-198).
        voice.volume = Clamp01(desc.volume);
        voice.looping = desc.loop;
        voice.priority = desc.priority;
        voice.bus = bus;
        voice.clip = desc.clip;
        voice.tag = desc.tag;
        voice.startSerial = ++state.serial;
        ma_sound_set_volume(&voice.sound, voice.volume);
        ma_sound_set_pitch(&voice.sound, SafePositive(desc.pitch, 1.0f));
        ma_sound_set_looping(&voice.sound, desc.loop ? MA_TRUE : MA_FALSE);
        if (desc.spatial)
        {
            const float minDistance = SafePositive(desc.minDistance, 1.0f);
            float maxDistance = SafePositive(desc.maxDistance, 50.0f);
            if (maxDistance < minDistance)
            {
                maxDistance = minDistance;
            }
            ma_sound_set_attenuation_model(&voice.sound, ToMiniaudio(desc.attenuation));
            ma_sound_set_min_distance(&voice.sound, minDistance);
            ma_sound_set_max_distance(&voice.sound, maxDistance);
            ma_sound_set_rolloff(&voice.sound, SafePositive(desc.rolloff, 1.0f));
            ma_sound_set_doppler_factor(&voice.sound, std::isfinite(desc.dopplerFactor) && desc.dopplerFactor > 0.0f
                ? desc.dopplerFactor : 0.0f);
            ma_sound_set_position(&voice.sound, desc.position[0], desc.position[1], desc.position[2]);
        }
        if (desc.fadeInSeconds > 0.0f && std::isfinite(desc.fadeInSeconds))
        {
            ma_sound_set_fade_in_milliseconds(&voice.sound, 0.0f, 1.0f,
                static_cast<ma_uint64>(desc.fadeInSeconds * 1000.0f));
        }
        if (desc.startDelaySeconds > 0.0f && std::isfinite(desc.startDelaySeconds))
        {
            const ma_uint64 delay = static_cast<ma_uint64>(desc.startDelaySeconds * static_cast<float>(state.desc.sampleRate));
            ma_sound_set_start_time_in_pcm_frames(&voice.sound, ma_engine_get_time_in_pcm_frames(&state.engine) + delay);
        }
        if (ma_sound_start(&voice.sound) != MA_SUCCESS)
        {
            voice.state = State::VoiceState::Paused;
            state.ReleaseVoice(index);
            return {};
        }
        voice.state = State::VoiceState::Playing;
        ++state.voicesStarted;
        return {index, voice.generation};
    }

    void AudioMixer::Stop(AudioVoiceHandle handle, float fadeOutSeconds)
    {
        if (false == IsInitialized())
        {
            return;
        }
        State::Voice* voice = m_state->Resolve(handle);
        if (voice == nullptr)
        {
            return;
        }
        if (fadeOutSeconds > 0.0f && std::isfinite(fadeOutSeconds) && voice->state == State::VoiceState::Playing)
        {
            ma_sound_stop_with_fade_in_milliseconds(&voice->sound, static_cast<ma_uint64>(fadeOutSeconds * 1000.0f));
            voice->state = State::VoiceState::Stopping;
            return;
        }
        m_state->ReleaseVoice(handle.index);
    }

    void AudioMixer::StopAllWithTag(std::uint32_t tag)
    {
        if (false == IsInitialized())
        {
            return;
        }
        for (std::uint32_t index = 0; index < m_state->voices.Size(); ++index)
        {
            const State::Voice& voice = m_state->voices[index];
            if (voice.state != State::VoiceState::Free && voice.tag == tag)
            {
                m_state->ReleaseVoice(index);
            }
        }
    }

    void AudioMixer::StopAll()
    {
        if (false == IsInitialized())
        {
            return;
        }
        for (std::uint32_t index = 0; index < m_state->voices.Size(); ++index)
        {
            m_state->ReleaseVoice(index);
        }
    }

    void AudioMixer::Pause(AudioVoiceHandle handle)
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice != nullptr && voice->state == State::VoiceState::Playing)
        {
            ma_sound_stop(&voice->sound);
            voice->state = State::VoiceState::Paused;
        }
    }

    void AudioMixer::Resume(AudioVoiceHandle handle)
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice != nullptr && voice->state == State::VoiceState::Paused)
        {
            ma_sound_start(&voice->sound);
            voice->state = State::VoiceState::Playing;
        }
    }

    bool AudioMixer::IsAlive(AudioVoiceHandle handle) const
    {
        return IsInitialized() && m_state->Resolve(handle) != nullptr;
    }

    bool AudioMixer::IsPaused(AudioVoiceHandle handle) const
    {
        const State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        return voice != nullptr && voice->state == State::VoiceState::Paused;
    }

    double AudioMixer::GetPlaybackSeconds(AudioVoiceHandle handle) const
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice == nullptr)
        {
            return 0.0;
        }
        float seconds = 0.0f;
        if (ma_sound_get_cursor_in_seconds(&voice->sound, &seconds) != MA_SUCCESS)
        {
            return 0.0;
        }
        return seconds;
    }

    void AudioMixer::SetVolume(AudioVoiceHandle handle, float volume)
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice != nullptr)
        {
            voice->volume = Clamp01(volume);
            ma_sound_set_volume(&voice->sound, voice->volume);
        }
    }

    void AudioMixer::SetPitch(AudioVoiceHandle handle, float pitch)
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice != nullptr)
        {
            ma_sound_set_pitch(&voice->sound, SafePositive(pitch, 1.0f));
        }
    }

    void AudioMixer::SetLooping(AudioVoiceHandle handle, bool loop)
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice != nullptr)
        {
            voice->looping = loop;
            ma_sound_set_looping(&voice->sound, loop ? MA_TRUE : MA_FALSE);
        }
    }

    void AudioMixer::SetPosition(AudioVoiceHandle handle, const float position[3])
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice != nullptr && position != nullptr)
        {
            ma_sound_set_position(&voice->sound, position[0], position[1], position[2]);
        }
    }

    void AudioMixer::SetVelocity(AudioVoiceHandle handle, const float velocity[3])
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice != nullptr && velocity != nullptr)
        {
            ma_sound_set_velocity(&voice->sound, velocity[0], velocity[1], velocity[2]);
        }
    }

    void AudioMixer::SetBus(AudioVoiceHandle handle, AudioBusId bus)
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice == nullptr || false == m_state->IsBusValid(bus) || voice->bus == bus)
        {
            return;
        }
        // 재생 중에도 스레드 안전하다(miniaudio `ma_node_attach_output_bus`, D-198). 보이스를 다시 만들지 않는다.
        ma_node_attach_output_bus(&voice->sound, 0, &m_state->buses[bus].group, 0);
        voice->bus = bus;
    }

    void AudioMixer::SetListener(const float position[3], const float forward[3], const float up[3])
    {
        if (false == IsInitialized())
        {
            return;
        }
        ma_engine* engine = &m_state->engine;
        if (position != nullptr)
        {
            ma_engine_listener_set_position(engine, 0, position[0], position[1], position[2]);
        }
        if (forward != nullptr)
        {
            ma_engine_listener_set_direction(engine, 0, forward[0], forward[1], forward[2]);
        }
        if (up != nullptr)
        {
            ma_engine_listener_set_world_up(engine, 0, up[0], up[1], up[2]);
        }
    }

    void AudioMixer::SetListenerVelocity(const float velocity[3])
    {
        if (IsInitialized() && velocity != nullptr)
        {
            ma_engine_listener_set_velocity(&m_state->engine, 0, velocity[0], velocity[1], velocity[2]);
        }
    }

    void AudioMixer::SetMasterVolume(float volume)
    {
        if (IsInitialized())
        {
            m_state->masterVolume = Clamp01(volume);
            ma_engine_set_volume(&m_state->engine, m_state->masterVolume);
        }
    }

    float AudioMixer::GetMasterVolume() const
    {
        return IsInitialized() ? m_state->masterVolume : 0.0f;
    }

    double AudioMixer::GetTimeSeconds() const
    {
        if (false == IsInitialized())
        {
            return 0.0;
        }
        return static_cast<double>(ma_engine_get_time_in_pcm_frames(&m_state->engine))
            / static_cast<double>(m_state->desc.sampleRate);
    }

    AudioMixer::Stats AudioMixer::GetStats() const
    {
        Stats stats;
        if (false == IsInitialized())
        {
            return stats;
        }
        const State& state = *m_state;
        stats.activeVoices = state.desc.maxVoices - static_cast<std::uint32_t>(state.freeVoices.Size());
        stats.registeredClips = static_cast<std::uint32_t>(state.clips.Size() - state.freeClips.Size());
        stats.voicesStarted = state.voicesStarted;
        stats.voicesStolen = state.voicesStolen;
        stats.voicesRejected = state.voicesRejected;
        stats.allocatorGrowths = state.allocator.GetGrowths();
        stats.lastPeak = state.peak.load(std::memory_order_relaxed);
        return stats;
    }
}
