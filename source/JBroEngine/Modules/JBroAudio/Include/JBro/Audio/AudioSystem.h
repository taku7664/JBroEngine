#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/AudioTypes/AudioBusName.h>
#include <JBro/AudioTypes/AudioTypes.h>
#include <JBro/AudioTypes/Internal/SystemContext.h>
#include <JBro/AudioTypes/ServiceContext.h>
#include <JBro/AudioTypes/System/IAudioSystem.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/Table.h>

#include <cstdint>

namespace JBro
{
    class AssetSystem;
    class AudioMixer;

    // 프로젝트 파일의 `AudioBuses` 한 줄이다(이름·시작 음량 0..1).
    struct AudioBusConfig
    {
        NameId name = InvalidNameId;
        float volume = 1.0f;
        AudioBusEffects effects;
        // 부모 버스 이름이다(D-203). 비우면 Master 다. 부모는 목록에서 **앞에** 있어야 한다 - 아니면 Master 아래로 두고 알린다.
        NameId parent = InvalidNameId;
        // 센드를 받을 버스 이름과 양(0..1)이다. 받는 버스는 목록의 어디에 있어도 된다. 되돌아오는 길이 생기면 끊고 알린다.
        NameId send = InvalidNameId;
        float sendLevel = 0.0f;
    };
}

namespace JBro::System
{
    // 출력 장치를 고르는 쪽이다(D-203). 장치는 호스트가 가지므로 호스트가 구현해 오디오 시스템에 건넨다.
    class IAudioDeviceControl
    {
    public:
        virtual ~IAudioDeviceControl() = default;
        // 목록을 새로 읽고 개수를 돌려준다.
        virtual std::uint32_t RefreshOutputDevices() = 0;
        virtual const char* GetOutputDeviceName(std::uint32_t index) const = 0;
        virtual const char* GetCurrentOutputDevice() const = 0;
        virtual bool SelectOutputDevice(const char* name) = 0;
        virtual bool IsWaitingForUserGesture() const = 0;
    };

    // 프로젝트 수명의 오디오 시스템이다(D-197). 차원과 무관한 몫을 한 곳에 둔다 - 버스 표, 에셋 → 클립 등록, 소스의
    // 상태 기계, 한 번 울리기, 에디터 미리 듣기. 차원별 시스템(`Audio2DSystem`·`Audio3DSystem`)은 위치와 리스너만
    // 채워 `UpdateSource`·`SetListener` 를 부른다. 스크립트 서비스는 `IAudioSystem` 으로 이것을 부른다.
    //
    // **메인 스레드 전용이다.** 믹서는 호스트가 소유하고 이것은 빌린다. 에셋 자료가 풀리기 직전에 에셋 시스템이
    // 알리므로(`AudioReleaseCallback`) 그 클립의 보이스를 먼저 멈추고 등록을 내린다.
    class AudioSystem final : public IAudioSystem
    {
    public:
        // 보이스 무리의 표지다. 게임 소리와 에디터 미리 듣기를 따로 멈춘다.
        static constexpr std::uint32_t GameTag = 1;
        static constexpr std::uint32_t PreviewTag = 2;
        // 한 번 울리기의 우선순위다. 소스 기본(128)보다 낮아 보이스가 모자라면 먼저 양보한다.
        static constexpr std::uint8_t OneShotPriority = 64;

        AudioSystem();
        ~AudioSystem() override;
        AudioSystem(const AudioSystem&) = delete;
        AudioSystem& operator=(const AudioSystem&) = delete;

        // 에셋 시스템이 없어도 선다(버스만 있고 클립은 없다). 풀 수 있는 에셋 시스템이면 해제 알림을 건다.
        bool Initialize(AudioMixer& mixer, AssetSystem* assets);
        // 게임 소리를 멈추고 클립 등록을 내리고 프로젝트 버스를 없앤다. 에셋 시스템을 내리기 전에 부른다.
        void Shutdown();
        bool IsInitialized() const;
        AudioMixer* GetMixer() const;

        // 프로젝트의 버스 목록으로 믹서의 버스를 다시 세운다. 그 버스의 보이스는 Master 로 옮겨진다.
        void ConfigureBuses(JArrayView<AudioBusConfig> buses);
        JArrayView<AudioBusConfig> GetBusConfigs() const;

        // 프레임마다 한 번(프레임워크 갱신 뒤). 끝난 보이스를 거둔다.
        void Update();

        // ── 차원별 시스템이 부른다 ────────────────────────────────────────────────────────────────
        // `planarDepth` 는 2D 의 몫이다: 소스를 듣는 자리 앞 이만큼의 깊이에 두어 가까운 소리가 한쪽 귀로 뚝 꺾이지
        // 않게 한다. 거리 감쇠는 같은 깊이만큼 보정한다. 3D 는 0 이다.
        // `deltaTime` 은 도플러의 속도(위치 차 / 시간)를 재는 데만 쓴다. 0 이면 속도를 0 으로 둔다.
        void SetListener(const float position[3], const float forward[3], const float up[3], float planarDepth,
            float deltaTime);
        // 소스 하나를 한 프레임 진행한다. `active` 는 `IsActiveComponent` 다.
        void UpdateSource(Component::AudioSource& source, bool active, const float position[3], float deltaTime);
        // 플레이를 멈출 때 부른다. 게임 소리를 전부 멈춘다(미리 듣기는 그대로다).
        void StopGameSounds();

        // ── 에디터 미리 듣기 ────────────────────────────────────────────────────────────────────
        // 미리 듣기 버스로 한 번에 하나만 울린다. 새로 부르면 앞의 것을 멈춘다. Master 음소거와 무관하게 들린다.
        bool PlayPreview(AssetHandle clip, bool loop);
        void StopPreview();
        bool IsPreviewPlaying() const;
        double GetPreviewTime() const;
        double GetPreviewDuration() const;
        // 미리 듣기 위치를 옮긴다(파형을 누른 자리).
        void SeekPreview(double seconds);
        AssetHandle GetPreviewClip() const;

        // 스크립트 DLL 에 건넬 값이다. 이 객체가 사는 동안 유효하다.
        const AudioSystemContext& GetSystemContext() const;
        const AudioServiceContext& GetServiceContext() const;

        // ── IAudioSystem ───────────────────────────────────────────────────────────────────────
        void PlayOneShot(AssetHandle clip, AudioBusName bus, float volume, float pitch) override;
        void PlayOneShotAt(AssetHandle clip, AudioBusName bus, float volume, float x, float y, float z) override;
        void PlaySource(Component::AudioSource& source) override;
        void StopSource(Component::AudioSource& source, float fadeOutSeconds) override;
        void PauseSource(Component::AudioSource& source) override;
        void ResumeSource(Component::AudioSource& source) override;
        bool IsSourcePlaying(const Component::AudioSource& source) const override;
        double GetSourceTime(const Component::AudioSource& source) const override;
        void ReleaseSource(Component::AudioSource& source) override;
        void SetBusVolume(AudioBusName bus, float volume) override;
        float GetBusVolume(AudioBusName bus) const override;
        void SetBusMuted(AudioBusName bus, bool muted) override;
        bool IsBusMuted(AudioBusName bus) const override;
        void SetBusEffects(AudioBusName bus, const AudioBusEffects& effects) override;
        AudioBusEffects GetBusEffects(AudioBusName bus) const override;
        void StopAll() override;

        // 출력 장치 쪽을 잇는다(호스트). null 이면 장치 목록이 비고 바꾸기는 거짓이다.
        void SetDeviceControl(IAudioDeviceControl* control);
        // 창의 포커스다(호스트가 입력 사건에서 알린다). `SetMuteWhenUnfocused` 가 켜져 있으면 소리를 줄이고 되돌린다.
        void SetWindowFocused(bool focused);

        std::uint32_t GetOutputDeviceCount() override;
        const char* GetOutputDeviceName(std::uint32_t index) const override;
        const char* GetOutputDevice() const override;
        bool SetOutputDevice(const char* name) override;
        bool IsWaitingForUserGesture() const override;
        void SetMuteWhenUnfocused(bool mute) override;
        bool IsMuteWhenUnfocused() const override;

        // ── 에디터의 믹싱 ─────────────────────────────────────────────────────────────────────────
        // 솔로는 저장하지 않는 믹싱 상태다. 버스를 다시 세워도(`ConfigureBuses`) 이름으로 되살린다.
        void SetBusSolo(AudioBusName bus, bool solo);
        bool IsBusSolo(AudioBusName bus) const;
        // 버스가 마지막으로 낸 블록의 최대 크기다(미터). Master 는 빈 이름이다.
        float GetBusPeak(AudioBusName bus) const;

    private:
        struct ClipEntry
        {
            AssetHandle asset;
            AudioClipHandle clip;
        };

        static std::uint64_t KeyOf(AssetHandle handle);
        static void OnAudioReleased(void* user, AssetHandle handle);
        AudioClipHandle AcquireClip(AssetHandle handle);
        void ReleaseClip(AssetHandle handle);
        AudioBusId ResolveBus(AudioBusName bus) const;
        void StartSource(Component::AudioSource& source);
        void StopVoice(Component::AudioSource& source, float fadeOutSeconds);
        void Place(const float position[3], float out[3]) const;
        float WidenDistance(float distance) const;

        AudioMixer* m_mixer = nullptr;
        AssetSystem* m_assets = nullptr;
        Table<std::uint64_t, ClipEntry> m_clips;
        Table<NameId, AudioBusId> m_buses;
        Array<AudioBusConfig> m_busConfigs;
        // 경고를 한 번만 남긴다. 목록에 없는 버스 이름을 처음 볼 때만 자란다(`const` 조회에서 쓰므로 mutable).
        mutable Table<NameId, bool> m_warnedBuses;
        Table<NameId, bool> m_soloBuses;
        IAudioDeviceControl* m_deviceControl = nullptr;
        bool m_muteWhenUnfocused = false;
        bool m_focused = true;
        AudioVoiceHandle m_preview;
        AssetHandle m_previewClip;
        float m_planarDepth = 0.0f;
        float m_listenerPosition[3] = {0.0f, 0.0f, 0.0f};
        bool m_listenerPlaced = false;
        AudioSystemContext m_systemContext;
        AudioServiceContext m_serviceContext;
        bool m_initialized = false;
    };
}
