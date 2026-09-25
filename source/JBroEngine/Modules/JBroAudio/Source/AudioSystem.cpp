#include <JBro/Audio/AudioSystem.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Audio/AudioMixer.h>
#include <JBro/AudioTypes/Component/AudioSource.h>
#include <JBro/Core/Log.h>
#include <JBro/Types/NameTable.h>

#include <cmath>

namespace JBro::System
{
    namespace
    {
        float Finite(float value, float fallback)
        {
            return std::isfinite(value) ? value : fallback;
        }

        std::uint8_t ClampPriority(std::int32_t priority)
        {
            if (priority < 0)
            {
                return 0;
            }
            return static_cast<std::uint8_t>(priority > 255 ? 255 : priority);
        }

        AudioClipDesc DescribeClip(const AudioData& data)
        {
            AudioClipDesc desc;
            desc.gain = data.options.gain;
            desc.frameCount = data.frameCount;
            desc.sampleRate = data.sampleRate;
            desc.channels = data.channels;
            if (data.options.mode == AudioImportMode::Streaming)
            {
                desc.encoding = AudioClipEncoding::Encoded;
                desc.bytes = data.encoded.Data();
                desc.byteCount = data.encoded.Size();
            }
            else if (data.options.mode == AudioImportMode::StreamFromDisk)
            {
                desc.encoding = AudioClipEncoding::File;
                desc.path = data.streamPath.c_str();
            }
            else
            {
                desc.encoding = AudioClipEncoding::Pcm;
                desc.pcm = data.pcm.Data();
            }
            return desc;
        }
    }

    AudioSystem::AudioSystem() = default;

    AudioSystem::~AudioSystem()
    {
        Shutdown();
    }

    bool AudioSystem::Initialize(AudioMixer& mixer, AssetSystem* assets)
    {
        if (m_initialized || false == mixer.IsInitialized())
        {
            return false;
        }
        m_mixer = &mixer;
        m_assets = assets;
        // 정상 프레임에서 표가 자라지 않게 미리 잡는다. 이보다 많은 클립을 한 프로젝트가 쓰면 그때 한 번 자란다.
        m_clips.Reserve(256);
        m_buses.Reserve(AudioMaxBuses);
        m_busConfigs.Reserve(AudioMaxBuses);
        if (m_assets != nullptr)
        {
            m_assets->SetAudioReleaseListener(&AudioSystem::OnAudioReleased, this);
        }
        m_systemContext = {};
        m_systemContext.Audio = this;
        m_serviceContext = {};
        m_initialized = true;
        return true;
    }

    void AudioSystem::Shutdown()
    {
        if (false == m_initialized)
        {
            return;
        }
        StopPreview();
        m_mixer->StopAllWithTag(GameTag);
        if (m_assets != nullptr)
        {
            m_assets->SetAudioReleaseListener(nullptr, nullptr);
        }
        for (auto it = m_clips.begin(); it != m_clips.end(); ++it)
        {
            m_mixer->UnregisterClip(it->MappedValue.clip);
        }
        m_clips.Clear();
        m_mixer->DestroyProjectBuses();
        m_buses.Clear();
        m_busConfigs.Clear();
        m_warnedBuses.Clear();
        m_soloBuses.Clear();
        // 믹서는 프로세스 수명이다 - 포커스 정책이 줄여 둔 출력을 다음 프로젝트에 넘기지 않는다.
        m_mixer->SetOutputGain(1.0f, 0.0f);
        m_muteWhenUnfocused = false;
        m_focused = true;
        m_deviceControl = nullptr;
        m_mixer = nullptr;
        m_assets = nullptr;
        m_systemContext = {};
        m_initialized = false;
    }

    bool AudioSystem::IsInitialized() const
    {
        return m_initialized;
    }

    AudioMixer* AudioSystem::GetMixer() const
    {
        return m_mixer;
    }

    void AudioSystem::ConfigureBuses(JArrayView<AudioBusConfig> buses)
    {
        if (false == m_initialized)
        {
            return;
        }
        m_mixer->DestroyProjectBuses();
        m_buses.Clear();
        m_busConfigs.Clear();
        m_warnedBuses.Clear();
        const NameId master = MakeNameId(AudioMasterBusName);
        for (std::uint32_t index = 0; index < buses.size; ++index)
        {
            const AudioBusConfig& config = buses.data[index];
            if (config.name == InvalidNameId || config.name == master || m_buses.Contains(config.name))
            {
                // Master 는 예약이다. 적힌 음량은 Master 에 건다. 겹친 이름은 첫 것만 산다.
                if (config.name == master)
                {
                    m_mixer->SetBusVolume(AudioMasterBus, config.volume);
                    m_mixer->SetBusEffects(AudioMasterBus, config.effects);
                }
                continue;
            }
            AudioBusId parent = AudioMasterBus;
            if (config.parent != InvalidNameId && config.parent != master)
            {
                if (const AudioBusId* found = m_buses.Find(config.parent))
                {
                    parent = *found;
                }
                else
                {
                    const char* text = NameTable::Get().Resolve(config.name);
                    Log::Write(LogLevel::Warning, "audio", "bus '%s': its parent must come earlier in the list - placing it under Master",
                        text[0] != '\0' ? text : "?");
                }
            }
            const AudioBusId bus = m_mixer->CreateBus(config.volume, parent);
            if (bus == AudioMasterBus)
            {
                break;
            }
            m_buses.TryAdd(config.name, bus);
            m_mixer->SetBusEffects(bus, config.effects);
            m_busConfigs.Add(config);
        }
        // 센드는 모든 버스가 선 뒤에 잇는다 - 받는 버스가 목록의 뒤에 있어도 된다.
        for (const AudioBusConfig& config : m_busConfigs)
        {
            if (config.send == InvalidNameId || config.sendLevel <= 0.0f)
            {
                continue;
            }
            const AudioBusId* from = m_buses.Find(config.name);
            const AudioBusId* to = m_buses.Find(config.send);
            if (from == nullptr || to == nullptr || false == m_mixer->SetBusSend(*from, *to, config.sendLevel))
            {
                const char* text = NameTable::Get().Resolve(config.name);
                Log::Write(LogLevel::Warning, "audio", "bus '%s': its send target is missing or would feed back - the send is off",
                    text[0] != '\0' ? text : "?");
            }
        }
        // 더킹도 모든 버스가 선 뒤에 잇는다(D-205).
        for (const AudioBusConfig& config : m_busConfigs)
        {
            if (config.duckBy == InvalidNameId || config.duckAmount <= 0.0f)
            {
                continue;
            }
            const AudioBusId* bus = m_buses.Find(config.name);
            const AudioBusId* trigger = m_buses.Find(config.duckBy);
            if (bus != nullptr && trigger != nullptr && *bus != *trigger)
            {
                m_mixer->SetBusDucking(*bus, *trigger, config.duckAmount, config.duckRelease);
            }
            else
            {
                const char* text = NameTable::Get().Resolve(config.name);
                Log::Write(LogLevel::Warning, "audio", "bus '%s': the bus it ducks under is missing - ducking is off",
                    text[0] != '\0' ? text : "?");
            }
        }
        // 솔로는 믹싱 상태라 파일에 없다. 버스를 다시 세워도 이름으로 되살린다.
        for (const auto& solo : m_soloBuses)
        {
            if (const AudioBusId* found = m_buses.Find(solo.KeyValue))
            {
                m_mixer->SetBusSolo(*found, solo.MappedValue);
            }
        }
    }

    JArrayView<AudioBusConfig> AudioSystem::GetBusConfigs() const
    {
        return {m_busConfigs.Data(), static_cast<std::uint32_t>(m_busConfigs.Size())};
    }

    void AudioSystem::Update()
    {
        if (m_initialized)
        {
            m_mixer->Update();
        }
    }

    std::uint64_t AudioSystem::KeyOf(AssetHandle handle)
    {
        return (static_cast<std::uint64_t>(handle.generation) << 32) | handle.index;
    }

    void AudioSystem::OnAudioReleased(void* user, AssetHandle handle)
    {
        static_cast<AudioSystem*>(user)->ReleaseClip(handle);
    }

    void AudioSystem::ReleaseClip(AssetHandle handle)
    {
        if (false == m_initialized)
        {
            return;
        }
        const std::uint64_t key = KeyOf(handle);
        if (const ClipEntry* entry = m_clips.Find(key))
        {
            // 믹서가 그 클립의 보이스를 멈춘 뒤에 돌아온다. 그 뒤에 에셋 시스템이 자료를 푼다.
            m_mixer->UnregisterClip(entry->clip);
            if (m_previewClip.index == handle.index && m_previewClip.generation == handle.generation)
            {
                m_preview = {};
                m_previewClip = {};
            }
            m_clips.Remove(key);
        }
    }

    AudioClipHandle AudioSystem::AcquireClip(AssetHandle handle)
    {
        if (false == m_initialized || handle.generation == 0 || m_assets == nullptr)
        {
            return {};
        }
        const std::uint64_t key = KeyOf(handle);
        if (const ClipEntry* entry = m_clips.Find(key))
        {
            if (m_mixer->IsClipRegistered(entry->clip))
            {
                return entry->clip;
            }
            m_clips.Remove(key);
        }
        const AudioData* data = m_assets->GetAudio(handle);
        if (data == nullptr)
        {
            return {};
        }
        const AudioClipHandle clip = m_mixer->RegisterClip(DescribeClip(*data));
        if (clip.IsSet())
        {
            m_clips.TryAdd(key, ClipEntry{handle, clip});
        }
        return clip;
    }

    AudioBusId AudioSystem::ResolveBus(AudioBusName bus) const
    {
        if (bus.IsMaster())
        {
            return AudioMasterBus;
        }
        if (const AudioBusId* found = m_buses.Find(bus.id))
        {
            return *found;
        }
        // 목록에 없는 이름은 Master 로 떨어지고 한 번만 알린다(기존 엔진 규약).
        if (false == m_warnedBuses.Contains(bus.id))
        {
            m_warnedBuses.TryAdd(bus.id, true);
            const char* text = NameTable::Get().Resolve(bus.id);
            Log::Write(LogLevel::Warning, "audio", "bus '%s' is not in the project's audio buses - playing on Master",
                text[0] != '\0' ? text : "?");
        }
        return AudioMasterBus;
    }

    void AudioSystem::SetListener(const float position[3], const float forward[3], const float up[3], float planarDepth,
        float deltaTime)
    {
        if (false == m_initialized || position == nullptr)
        {
            return;
        }
        m_planarDepth = planarDepth > 0.0f && std::isfinite(planarDepth) ? planarDepth : 0.0f;
        m_mixer->SetListener(position, forward, up);
        // 리스너의 속도는 도플러에만 쓰인다. 첫 프레임과 순간 이동(시간 0)은 0 이다.
        float velocity[3] = {0.0f, 0.0f, 0.0f};
        if (m_listenerPlaced && deltaTime > 0.0f && std::isfinite(deltaTime))
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                velocity[axis] = (position[axis] - m_listenerPosition[axis]) / deltaTime;
            }
        }
        m_mixer->SetListenerVelocity(velocity);
        for (int axis = 0; axis < 3; ++axis)
        {
            m_listenerPosition[axis] = position[axis];
        }
        m_listenerPlaced = true;
    }

    void AudioSystem::Place(const float position[3], float out[3]) const
    {
        out[0] = Finite(position[0], 0.0f);
        out[1] = Finite(position[1], 0.0f);
        // 2D 는 소스를 듣는 자리 앞(-Z) 깊이에 둔다. 리스너가 z=0 에서 -Z 를 보고 있다.
        out[2] = Finite(position[2], 0.0f) - m_planarDepth;
    }

    float AudioSystem::WidenDistance(float distance) const
    {
        // 깊이만큼 멀어진 것을 보정한다: 평면에서 d 만큼 떨어진 소리는 실제로 sqrt(d² + 깊이²) 에 있다.
        return m_planarDepth > 0.0f ? std::sqrt(distance * distance + m_planarDepth * m_planarDepth) : distance;
    }

    void AudioSystem::StopVoice(Component::AudioSource& source, float fadeOutSeconds)
    {
        if (source.runtime.voice.IsSet())
        {
            m_mixer->Stop(source.runtime.voice, fadeOutSeconds);
            source.runtime.voice = {};
        }
    }

    void AudioSystem::StartSource(Component::AudioSource& source)
    {
        Component::AudioSource::Runtime& runtime = source.runtime;
        StopVoice(source, 0.0f);
        runtime.playingClip = source.clip;
        const AudioClipHandle clip = AcquireClip(source.clip);
        if (false == clip.IsSet())
        {
            source.state = Component::AudioSourceState::NoClip;
            return;
        }
        AudioPlayDesc play;
        play.lowPassHz = Finite(source.lowPass, 0.0f);
        play.highPassHz = Finite(source.highPass, 0.0f);
        play.clip = clip;
        play.bus = ResolveBus(source.bus);
        play.volume = Finite(source.volume, 1.0f);
        play.pitch = Finite(source.pitch, 1.0f);
        play.loop = source.loop;
        play.spatial = source.spatial;
        play.attenuation = source.attenuation;
        play.minDistance = WidenDistance(Finite(source.minDistance, 1.0f));
        play.maxDistance = WidenDistance(Finite(source.maxDistance, 50.0f));
        play.rolloff = Finite(source.rolloff, 1.0f);
        play.dopplerFactor = Finite(source.doppler, 0.0f);
        Place(runtime.position, play.position);
        play.priority = ClampPriority(source.priority);
        play.fadeInSeconds = Finite(source.fadeIn, 0.0f);
        play.tag = GameTag;
        runtime.voice = m_mixer->Play(play);
        runtime.lastVolume = play.volume;
        runtime.lastPitch = play.pitch;
        runtime.lastLoop = play.loop;
        runtime.lastBus = source.bus;
        runtime.lastLowPass = play.lowPassHz;
        runtime.lastHighPass = play.highPassHz;
        // 보이스가 모자라 거절되면 끝난 것과 같다 - 매 프레임 다시 시도하지 않는다.
        source.state = runtime.voice.IsSet() ? Component::AudioSourceState::Playing : Component::AudioSourceState::Finished;
    }

    void AudioSystem::UpdateSource(Component::AudioSource& source, bool active, const float position[3], float deltaTime)
    {
        if (false == m_initialized)
        {
            return;
        }
        Component::AudioSource::Runtime& runtime = source.runtime;
        if (false == active)
        {
            // 끄면 곧바로 멈추고, 다시 켜면 `playOnStart` 가 한 번 다시 무장된다(기존 엔진 단계 3 의 정책).
            if (runtime.wasActive)
            {
                StopVoice(source, 0.0f);
                source.state = Component::AudioSourceState::Idle;
                runtime.wasActive = false;
                runtime.playOnStartUsed = false;
                runtime.playRequested = false;
                runtime.stoppedByScript = false;
            }
            return;
        }
        float velocity[3] = {0.0f, 0.0f, 0.0f};
        if (position != nullptr)
        {
            // 도플러를 쓰는 소스만 속도를 잰다. 첫 프레임(보이스 없음)은 0 이다.
            if (source.doppler > 0.0f && runtime.voice.IsSet() && deltaTime > 0.0f && std::isfinite(deltaTime))
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    velocity[axis] = (position[axis] - runtime.position[axis]) / deltaTime;
                }
            }
            runtime.position[0] = position[0];
            runtime.position[1] = position[1];
            runtime.position[2] = position[2];
        }
        runtime.wasActive = true;

        // 보이스가 끝났거나 훔쳐졌다.
        if (runtime.voice.IsSet() && false == m_mixer->IsAlive(runtime.voice))
        {
            runtime.voice = {};
            if (source.state == Component::AudioSourceState::Playing)
            {
                source.state = Component::AudioSourceState::Finished;
            }
        }
        // 클립이 바뀌었다. 재생 중이었으면 새 클립으로 잇고, 아니면 다음 시작이 새 클립을 쓴다.
        const bool clipChanged = source.clip.index != runtime.playingClip.index
            || source.clip.generation != runtime.playingClip.generation;
        if (clipChanged)
        {
            const bool wasPlaying = runtime.voice.IsSet();
            StopVoice(source, 0.0f);
            runtime.playingClip = source.clip;
            if (source.state == Component::AudioSourceState::NoClip)
            {
                // 클립이 없어 울리지 못했던 것이다. 클립이 생기면 `playOnStart` 를 한 번 다시 무장한다 - 인스펙터에서
                // 플레이 중에 클립을 고르면 들려야 한다.
                source.state = Component::AudioSourceState::Idle;
                runtime.playOnStartUsed = false;
            }
            else if (source.state == Component::AudioSourceState::Finished)
            {
                source.state = Component::AudioSourceState::Idle;
            }
            if (wasPlaying)
            {
                runtime.playRequested = true;
            }
        }

        const bool autoStart = source.playOnStart && false == runtime.playOnStartUsed && false == runtime.stoppedByScript;
        if (runtime.playRequested || autoStart)
        {
            runtime.playRequested = false;
            runtime.playOnStartUsed = true;
            StartSource(source);
            return;
        }
        if (false == runtime.voice.IsSet())
        {
            return;
        }
        // 재생 중에 바뀔 수 있는 값만 민다(miniaudio 의 원자 변수). 바뀌지 않았으면 부르지 않는다.
        const float volume = Finite(source.volume, 1.0f);
        if (volume != runtime.lastVolume)
        {
            m_mixer->SetVolume(runtime.voice, volume);
            runtime.lastVolume = volume;
        }
        const float pitch = Finite(source.pitch, 1.0f);
        if (pitch != runtime.lastPitch)
        {
            m_mixer->SetPitch(runtime.voice, pitch);
            runtime.lastPitch = pitch;
        }
        if (source.loop != runtime.lastLoop)
        {
            m_mixer->SetLooping(runtime.voice, source.loop);
            runtime.lastLoop = source.loop;
        }
        if (false == (source.bus == runtime.lastBus))
        {
            m_mixer->SetBus(runtime.voice, ResolveBus(source.bus));
            runtime.lastBus = source.bus;
        }
        const float lowPass = Finite(source.lowPass, 0.0f);
        const float highPass = Finite(source.highPass, 0.0f);
        if (lowPass != runtime.lastLowPass || highPass != runtime.lastHighPass)
        {
            m_mixer->SetVoiceFilter(runtime.voice, lowPass, highPass);
            runtime.lastLowPass = lowPass;
            runtime.lastHighPass = highPass;
        }
        if (source.spatial)
        {
            float placed[3];
            Place(runtime.position, placed);
            m_mixer->SetPosition(runtime.voice, placed);
            if (source.doppler > 0.0f)
            {
                m_mixer->SetVelocity(runtime.voice, velocity);
            }
        }
    }

    void AudioSystem::StopGameSounds()
    {
        if (m_initialized)
        {
            m_mixer->StopAllWithTag(GameTag);
        }
    }

    bool AudioSystem::PlayPreview(AssetHandle clip, bool loop)
    {
        StopPreview();
        const AudioClipHandle registered = AcquireClip(clip);
        if (false == registered.IsSet())
        {
            return false;
        }
        AudioPlayDesc play;
        play.clip = registered;
        play.bus = AudioEditorPreviewBus;
        play.loop = loop;
        play.priority = 255;
        play.tag = PreviewTag;
        m_preview = m_mixer->Play(play);
        m_previewClip = m_preview.IsSet() ? clip : AssetHandle{};
        return m_preview.IsSet();
    }

    void AudioSystem::StopPreview()
    {
        if (m_initialized && m_preview.IsSet())
        {
            m_mixer->Stop(m_preview);
        }
        m_preview = {};
        m_previewClip = {};
    }

    bool AudioSystem::IsPreviewPlaying() const
    {
        return m_initialized && m_mixer->IsAlive(m_preview);
    }

    double AudioSystem::GetPreviewTime() const
    {
        return m_initialized ? m_mixer->GetPlaybackSeconds(m_preview) : 0.0;
    }

    double AudioSystem::GetPreviewDuration() const
    {
        if (false == m_initialized || m_assets == nullptr)
        {
            return 0.0;
        }
        const AudioData* data = m_assets->GetAudio(m_previewClip);
        return data != nullptr && data->sampleRate > 0 ? static_cast<double>(data->frameCount) / data->sampleRate : 0.0;
    }

    void AudioSystem::SeekPreview(double seconds)
    {
        if (m_initialized)
        {
            m_mixer->Seek(m_preview, seconds);
        }
    }

    AssetHandle AudioSystem::GetPreviewClip() const
    {
        return IsPreviewPlaying() ? m_previewClip : AssetHandle{};
    }

    const AudioSystemContext& AudioSystem::GetSystemContext() const
    {
        return m_systemContext;
    }

    const AudioServiceContext& AudioSystem::GetServiceContext() const
    {
        return m_serviceContext;
    }

    void AudioSystem::PlayOneShot(AssetHandle clip, AudioBusName bus, float volume, float pitch)
    {
        const AudioClipHandle registered = AcquireClip(clip);
        if (false == registered.IsSet())
        {
            return;
        }
        AudioPlayDesc play;
        play.clip = registered;
        play.bus = ResolveBus(bus);
        play.volume = Finite(volume, 1.0f);
        play.pitch = Finite(pitch, 1.0f);
        play.priority = OneShotPriority;
        play.tag = GameTag;
        m_mixer->Play(play);
    }

    void AudioSystem::PlayOneShotAt(AssetHandle clip, AudioBusName bus, float volume, float x, float y, float z)
    {
        const AudioClipHandle registered = AcquireClip(clip);
        if (false == registered.IsSet())
        {
            return;
        }
        AudioPlayDesc play;
        play.clip = registered;
        play.bus = ResolveBus(bus);
        play.volume = Finite(volume, 1.0f);
        play.spatial = true;
        play.minDistance = WidenDistance(1.0f);
        play.maxDistance = WidenDistance(50.0f);
        const float position[3] = {x, y, z};
        Place(position, play.position);
        play.priority = OneShotPriority;
        play.tag = GameTag;
        m_mixer->Play(play);
    }

    void AudioSystem::PlaySource(Component::AudioSource& source)
    {
        if (false == m_initialized)
        {
            return;
        }
        // 위치는 다음 갱신이 채운다. 이미 울리고 있으면 처음부터 다시 한다.
        source.runtime.playRequested = true;
        source.runtime.stoppedByScript = false;
    }

    void AudioSystem::StopSource(Component::AudioSource& source, float fadeOutSeconds)
    {
        if (false == m_initialized)
        {
            return;
        }
        StopVoice(source, fadeOutSeconds);
        source.runtime.playRequested = false;
        source.runtime.stoppedByScript = true;
        source.state = Component::AudioSourceState::Idle;
    }

    void AudioSystem::PauseSource(Component::AudioSource& source)
    {
        if (m_initialized && m_mixer->IsAlive(source.runtime.voice))
        {
            m_mixer->Pause(source.runtime.voice);
            source.state = Component::AudioSourceState::Paused;
        }
    }

    void AudioSystem::ResumeSource(Component::AudioSource& source)
    {
        if (m_initialized && m_mixer->IsPaused(source.runtime.voice))
        {
            m_mixer->Resume(source.runtime.voice);
            source.state = Component::AudioSourceState::Playing;
        }
    }

    bool AudioSystem::IsSourcePlaying(const Component::AudioSource& source) const
    {
        if (false == m_initialized)
        {
            return false;
        }
        if (source.runtime.playRequested)
        {
            return true;
        }
        return m_mixer->IsAlive(source.runtime.voice) && false == m_mixer->IsPaused(source.runtime.voice);
    }

    double AudioSystem::GetSourceTime(const Component::AudioSource& source) const
    {
        return m_initialized ? m_mixer->GetPlaybackSeconds(source.runtime.voice) : 0.0;
    }

    void AudioSystem::ReleaseSource(Component::AudioSource& source)
    {
        if (m_initialized)
        {
            StopVoice(source, 0.0f);
        }
        source.runtime = {};
        source.state = Component::AudioSourceState::Idle;
    }

    void AudioSystem::SetBusVolume(AudioBusName bus, float volume)
    {
        if (m_initialized)
        {
            m_mixer->SetBusVolume(ResolveBus(bus), volume);
        }
    }

    float AudioSystem::GetBusVolume(AudioBusName bus) const
    {
        return m_initialized ? m_mixer->GetBusVolume(ResolveBus(bus)) : 0.0f;
    }

    void AudioSystem::SetBusMuted(AudioBusName bus, bool muted)
    {
        if (m_initialized)
        {
            m_mixer->SetBusMuted(ResolveBus(bus), muted);
        }
    }

    bool AudioSystem::IsBusMuted(AudioBusName bus) const
    {
        return m_initialized && m_mixer->IsBusMuted(ResolveBus(bus));
    }

    void AudioSystem::SetBusEffects(AudioBusName bus, const AudioBusEffects& effects)
    {
        if (m_initialized)
        {
            m_mixer->SetBusEffects(ResolveBus(bus), effects);
        }
    }

    AudioBusEffects AudioSystem::GetBusEffects(AudioBusName bus) const
    {
        return m_initialized ? m_mixer->GetBusEffects(ResolveBus(bus)) : AudioBusEffects{};
    }

    void AudioSystem::StopAll()
    {
        StopGameSounds();
    }

    void AudioSystem::FadeBusVolume(AudioBusName bus, float volume, float seconds)
    {
        if (m_initialized)
        {
            m_mixer->SetBusVolume(ResolveBus(bus), volume, seconds);
        }
    }

    void AudioSystem::SetBusSolo(AudioBusName bus, bool solo)
    {
        if (false == m_initialized || bus.IsMaster())
        {
            return;
        }
        if (solo)
        {
            m_soloBuses.InsertOrAssign(bus.id, true);
        }
        else
        {
            m_soloBuses.Remove(bus.id);
        }
        if (const AudioBusId* found = m_buses.Find(bus.id))
        {
            m_mixer->SetBusSolo(*found, solo);
        }
    }

    bool AudioSystem::IsBusSolo(AudioBusName bus) const
    {
        return m_initialized && false == bus.IsMaster() && m_soloBuses.Contains(bus.id);
    }

    float AudioSystem::GetBusPeak(AudioBusName bus) const
    {
        return m_initialized ? m_mixer->GetBusPeak(ResolveBus(bus)) : 0.0f;
    }

    void AudioSystem::SetDeviceControl(IAudioDeviceControl* control)
    {
        m_deviceControl = control;
    }

    void AudioSystem::SetWindowFocused(bool focused)
    {
        if (m_focused == focused)
        {
            return;
        }
        m_focused = focused;
        if (m_initialized && m_muteWhenUnfocused)
        {
            m_mixer->SetOutputGain(focused ? 1.0f : 0.0f, 0.2f);
        }
    }

    std::uint32_t AudioSystem::GetOutputDeviceCount()
    {
        return m_deviceControl != nullptr ? m_deviceControl->RefreshOutputDevices() : 0;
    }

    const char* AudioSystem::GetOutputDeviceName(std::uint32_t index) const
    {
        return m_deviceControl != nullptr ? m_deviceControl->GetOutputDeviceName(index) : "";
    }

    const char* AudioSystem::GetOutputDevice() const
    {
        return m_deviceControl != nullptr ? m_deviceControl->GetCurrentOutputDevice() : "";
    }

    bool AudioSystem::SetOutputDevice(const char* name)
    {
        return m_deviceControl != nullptr && m_deviceControl->SelectOutputDevice(name);
    }

    bool AudioSystem::IsWaitingForUserGesture() const
    {
        return m_deviceControl != nullptr && m_deviceControl->IsWaitingForUserGesture();
    }

    void AudioSystem::SetMuteWhenUnfocused(bool mute)
    {
        m_muteWhenUnfocused = mute;
        if (m_initialized)
        {
            // 정책을 바꾸는 순간의 포커스로 곧바로 맞춘다 - 포커스 없이 켜면 곧 줄고, 끄면 곧 돌아온다.
            m_mixer->SetOutputGain(mute && false == m_focused ? 0.0f : 1.0f, 0.2f);
        }
    }

    bool AudioSystem::IsMuteWhenUnfocused() const
    {
        return m_muteWhenUnfocused;
    }
}
